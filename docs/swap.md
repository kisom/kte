# Swap journaling (crash recovery)

kte has a small “swap” system: an append-only per-buffer journal that
records edits so they can be replayed after a crash.

This document describes the **currently implemented** swap system (stage
2), as implemented in `Swap.h` / `Swap.cc`.

## What it is (and what it is not)

- The swap file is a **journal of editing operations** (currently
  inserts, deletes, and periodic full-buffer checkpoints).
- It is written by a **single background writer thread** owned by
  `kte::SwapManager`.
- It is intended for **best-effort crash recovery**.

kte automatically deletes/resets swap journals after a **clean save**
and when
closing a clean buffer, so old swap files do not accumulate under normal
workflows. A best-effort prune also runs at startup to remove very old
swap
files.

## Automatic recovery prompt

When kte opens a file-backed buffer, it checks whether a corresponding
swap journal exists.

- If a swap file exists and replay succeeds *and* produces different
  content than what is currently on disk, kte prompts:

  ```text
  Recover swap edits for <path>? (y/N, C-g cancel)
  ```

    - `y`: open the file and apply swap replay (buffer becomes dirty)
    - `Enter` (default) / any non-`y`: delete the swap file (
      best-effort)
      and open the file normally
    - `C-g`: cancel opening the file

- If a swap file exists but is unreadable/corrupt, kte prompts:

  ```text
  Swap file unreadable for <path>. Delete it? (y/N, C-g cancel)
  ```

    - `y`: delete the swap file (best-effort) and open the file normally
    - `Enter` (default): keep the swap file and open the file normally
    - `C-g`: cancel opening the file

## Where swap files live

Swap files are stored under an XDG-style per-user *state* directory:

- If `XDG_STATE_HOME` is set and non-empty:
    - `$XDG_STATE_HOME/kte/swap/…`
- Otherwise, if `HOME` is set:
    - `~/.local/state/kte/swap/…`
- Last resort fallback:
    - `<system-temp>/kte/state/kte/swap/…` (via
      `std::filesystem::temp_directory_path()`)

Swap files are always created with permissions `0600`.

### Swap file naming

For file-backed buffers, the swap filename is derived from the buffer’s
path:

1. Take a canonical-ish path key (`std::filesystem::weakly_canonical`,
   else `absolute`, else the raw `Buffer::Filename()`).
2. Encode it so it’s human-identifiable:
    - Strip one leading path separator (`/` or `\\`)
    - Replace path separators (`/` and `\\`) with `!`
    - Append `.swp`

Example:

```text
/home/kyle/tmp/test.txt  ->  home!kyle!tmp!test.txt.swp
```

If the resulting name would be long (over ~200 characters), kte falls
back to a shorter stable name:

```text
<basename>.<fnv1a64(path-key-as-hex)>.swp
```

For unnamed/unsaved buffers, kte uses:

```text
unnamed-<pid>-<counter>.swp
```

## Lifecycle (when swap is written)

`kte::SwapManager` is owned by `Editor` (see `Editor.cc`). Buffers are
attached for journaling when they are added/opened.

- `SwapManager::Attach(Buffer*)` starts tracking a buffer and
  establishes its swap path.
- `Buffer` emits swap events from its low-level edit APIs:
    - `Buffer::insert_text()` calls `SwapRecorder::OnInsert()`
    - `Buffer::delete_text()` calls `SwapRecorder::OnDelete()`
    - `Buffer::split_line()` / `join_lines()` are represented as
      insert/delete of `\n` (they do **not** emit `SPLIT`/`JOIN` records
      in stage 1).
- `SwapManager::Detach(Buffer*)` flushes queued records, `fsync()`s, and
  closes the journal.
- On `Save As` / filename changes,
  `SwapManager::NotifyFilenameChanged(Buffer&)` closes the existing
  journal and switches to a new path.
    - Note: the old swap file is currently left on disk (no
      cleanup/rotation yet).

## Durability and performance

Swap writing is best-effort and asynchronous:

- Records are queued from the UI/editing thread(s).
- A background writer thread wakes at least every
  `SwapConfig::flush_interval_ms` (default `200ms`) to write any queued
  records.
- `fsync()` is throttled to at most once per
  `SwapConfig::fsync_interval_ms` (default `1000ms`) per open swap file.
- `SwapManager::Flush()` blocks until the queue is fully written; it is
  primarily used by tests and shutdown paths.

If a crash happens while writing, the swap file may end with a partial
record. Replay detects truncation/CRC mismatch and fails safely.

## On-disk format (v1)

The file is:

1. A fixed-size 64-byte header
2. Followed by a stream of records

All multi-byte integers in the swap file are **little-endian**.

### Header (64 bytes)

Layout (stage 1):

- `magic` (8 bytes): `KTE_SWP\0`
- `version` (`u32`): currently `1`
- `flags` (`u32`): currently `0`
- `created_time` (`u64`): Unix seconds
- remaining bytes are reserved/padding (currently zeroed)

### Record framing

Each record is:

```text
[type: u8][len: u24][payload: len bytes][crc32: u32]
```

- `len` is a 24-bit little-endian length of the payload (`0..0xFFFFFF`).
- `crc32` is computed over the 4-byte record header (`type + len`)
  followed by the payload bytes.

### Record types

Type codes are defined in `SwapRecType` (`Swap.h`). Stage 1 primarily
emits:

- `INS` (`1`): insert bytes at `(row, col)`
- `DEL` (`2`): delete `len` bytes at `(row, col)`

Other type codes exist for forward compatibility (`SPLIT`, `JOIN`,
`META`, `CHKPT`), but are not produced by the current `SwapRecorder`
interface.

### Payload encoding (v1)

Every payload starts with:

```text
[encver: u8]
```

Currently `encver` must be `1`.

#### `INS` payload (encver = 1)

```text
[encver: u8 = 1]
[row:   u32]
[col:   u32]
[nbytes:u32]
[bytes: nbytes]
```

#### `DEL` payload (encver = 1)

```text
[encver: u8 = 1]
[row:   u32]
[col:   u32]
[len:   u32]
```

`row`/`col` are 0-based and are interpreted the same way as
`Buffer::insert_text()` / `Buffer::delete_text()`.

## Replay / recovery

Swap replay is implemented as a low-level API:

-

`bool kte::SwapManager::ReplayFile(Buffer &buf, const std::string &swap_path, std::string &err)`

Behavior:

- The caller supplies an **already-open** `Buffer` (typically loaded
  from the on-disk file) and a swap path.
- `ReplayFile()` validates header magic/version, then iterates records.
- On a truncated file or CRC mismatch, it returns `false` and sets
  `err`.
- On unknown record types, it ignores them (forward compatibility).
- On failure, the buffer may have had a prefix of records applied;
  callers should treat this as “recovery failed”.

Important: if the buffer is currently attached to a `SwapManager`, you
should suspend/disable recording during replay (or detach first),
otherwise replayed edits would be re-journaled.

## Tests

Swap behavior and format are validated by unit tests:

- `tests/test_swap_writer.cc` (header, permissions, record CRC framing)
- `tests/test_swap_replay.cc` (record replay and truncation handling)
