Swap files for kte — design plan
================================

Goals
-----

- Preserve user work across crashes, power failures, and OS kills.
- Keep the editor responsive; avoid blocking the UI on disk I/O.
- Bound recovery time and swap size.
- Favor simple, robust primitives that work well on POSIX and macOS;
  keep Windows feasibility in mind.

Model overview
--------------
Per open buffer, maintain a sidecar swap journal next to the file:

- Path: `.<basename>.kte.swp` in the same directory as the file (for
  unnamed/unsaved buffers, use a per‑session temp dir like
  `$TMPDIR/kte/` with a random UUID).
- Format: append‑only journal of editing operations with periodic
  checkpoints.
- Crash safety: only append, fsync as per policy; checkpoint via
  write‑to‑temp + fsync + atomic rename.

File format (v1)
----------------
Header (fixed 64 bytes):

- Magic: `KTE_SWP\0` (8 bytes)
- Version: 1 (u32)
- Flags: bitset (u32) — e.g., compression, checksums, endian.
- Created time (u64)
- Host info hash (u64) — optional, for telemetry/debug.
- File identity: hash of canonical path (u64) and original file
  size+mtime (u64+u64) at start.
- Reserved/padding.

Records (stream after header):

- Each record: [type u8][len u24][payload][crc32 u32]
- Types:
    - `CHKPT` — full snapshot checkpoint of entire buffer content and
      minimal metadata (cursor pos, filetype). Payload optionally
      compressed. Written occasionally to cap replay time.
    - `INS` — insert at (row, col) text bytes (text may contain
      newlines). Encoded with varints.
    - `DEL` — delete length at (row, col). If spanning lines, semantics
      defined as in Buffer::delete_text.
    - `SPLIT`, `JOIN` — explicit structural ops (optional; can be
      expressed via INS/DEL).
    - `META` — update metadata (e.g., filetype, encoding hints).

Durability policy
-----------------
Configurable knobs (sane defaults in parentheses):

- Time‑based flush: group edits and flush every 150–300 ms (200 ms).
- Operation count flush: after N ops (200).
- Idle flush: on 500 ms idle lull, flush immediately.
- Checkpoint cadence: after M KB of journal (512–2048 KB) or T seconds (
  30–120 s), whichever first.
- fsync policy:
    - `always`: fsync every flush (safest, slowest).
    - `grouped` (default): fsync at most every 1–2 s or on
      idle/blur/quit.
    - `never`: rely on OS flush (fastest, riskier).
    - On POSIX, prefer `fdatasync` when available; fall back to `fsync`.

Performance & threading
-----------------------

- Background writer thread per editor instance (shared) with a bounded
  MPSC queue of per‑buffer records.
- Each Buffer has a small in‑memory journal buffer; UI thread enqueues
  ops (non‑blocking) and may coalesce adjacent inserts/deletes.
- Writer batch‑writes records to the swap file, computes CRCs, and
  decides checkpoint boundaries.
- Backpressure: if the queue grows beyond a high watermark, signal the
  UI to start coalescing more aggressively and slow enqueue (never block
  hard editing path; at worst drop optional `META`).

Recovery flow
-------------

On opening a file:

1. Detect swap sidecar `.<basename>.kte.swp`.
2. Validate header, iterate records verifying CRCs.
3. Compare recorded original file identity against actual file; if
   mismatch, warn user but allow recovery (content wins).
4. Reconstruct buffer: start from the last good `CHKPT` (if any), then
   replay subsequent ops. If trailing partial record encountered (EOF
   mid‑record), truncate at last good offset.
5. Present a choice: Recover (load recovered buffer; keep the swap file
   until user saves) or Discard (delete swap file and open clean file).

Stability & corruption mitigation
---------------------------------

- Append‑only with per‑record CRC32 guards against torn writes.
- Atomic checkpoint rotation: write `.<basename>.kte.swp.tmp`, fsync,
  then rename over old `.swp`.
- Size caps: rotate or compact when `.swp` exceeds a threshold (e.g.,
  64–128 MB). Compaction creates a fresh file with a single checkpoint.
- Low‑disk‑space behavior: on write failures, surface a non‑modal
  warning and temporarily fall back to in‑memory only; retry
  opportunistically.

Security considerations
-----------------------

- Swap files mirror buffer content, which may be sensitive. Options:
    - Configurable location (same dir vs. `$XDG_STATE_HOME/kte/swap`).
    - Optional per‑file encryption (future work) using OS keychain.
    - Ensure permissions are 0600.

Interoperability & UX
---------------------

- Use a distinctive extension `.kte.swp` to avoid conflicts with other
  editors.
- Status bar indicator when swap is active; commands to purge/compact.
- On save: do not delete swap immediately; keep until the buffer is
  clean and idle for a short grace period (allows undo of accidental
  external changes).

Implementation plan (staged)
----------------------------

1. Minimal journal writer (append‑only INS/DEL) with grouped fsync;
   single per‑editor writer thread.
2. Reader/recovery path with CRC validation and replay.
3. Checkpoints + atomic rotation; compaction path.
4. Config surface and UI prompts; telemetry counters.
5. Optional compression and advanced coalescing.

Defaults balancing performance and stability
-------------------------------------------

- Grouped flush with fsync every ~1 s or on idle/quit.
- Checkpoint every 1 MB or 60 s.
- Bounded queue and batch writes to minimize syscalls.
- Immediate flush on critical events (buffer close, app quit, power
  source change on laptops if detectable).
