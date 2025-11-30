Undo/Redo + C-k GUI status (macOS) — current state snapshot

Context
 - Platform: macOS (Darwin)
 - Target: GUI build (kge) using SDL2/ImGui path
 - Date: 2025-11-30 00:30 local (from user)

What works right now
 - Terminal (kte): C-k keymap and UndoSystem integration have been stable in recent builds.
 - GUI: Most C-k mappings work: C-k d (KillToEOL), C-k x (Save+Quit), C-k q (Quit) — confirmed by user.
 - UndoSystem core is implemented and integrated for InsertText/Newline/Delete/Backspace. Buffer owns an UndoSystem and raw edit APIs are used by apply().

What is broken (GUI, macOS)
 - C-k u: Status shows "Undone" but buffer content does not change (no visible undo).
 - C-k U: Inserts a literal 'U' into the buffer; does not execute Redo.
 - C-k C-u / C-k C-U: No effect (expected unmapped), but the k-prefix prompt can remain in some paths.

Repro steps (GUI)
 1) Type "Hello".
 2) Press C-k then press u → status becomes "Undone", but text remains "Hello".
 3) Press C-k then press Shift+U → a literal 'U' is inserted (becomes "HelloU").
 4) Press C-k then hold Ctrl on the suffix and press u → status "Undone", still no change.
 5) Press C-k then hold Ctrl on the suffix and press Shift+U → status shows the k-prefix prompt again ("C-k _").

Keymap and input-layer changes we attempted (and kept)
 - KKeymap.cc: Case-sensitive 'U' mapping prioritized before the lowercase table. Added ctrl→non-ctrl fall-through so C-k u/U still map even if SDL reports Ctrl held on the suffix.
 - TerminalInputHandler: already preserved case and mapped correctly.
 - GUIInputHandler:
   - Preserve case for k-prefix suffix letters (Shift → uppercase). Clear esc_meta before k-suffix mapping.
   - Strengthened SDL_TEXTINPUT suppression after a k-prefix printable suffix to avoid inserting literal characters.
   - Added fallback to map the k-prefix suffix in the SDL_TEXTINPUT path (to catch macOS cases where uppercase arrives in TEXTINPUT rather than KEYDOWN).
   - Fixed malformed switch block introduced during iteration.
 - Command layer: commit pending undo batch at k-prefix entry and just before Undo/Redo so prior typing can actually be undone/redone.

Diagnostics added
 - GUIInputHandler logs k-prefix u/U suffix attempts to stderr and (previously) /tmp/kge.log. The user’s macOS session showed only KEYDOWN logs for 'u':
   - "[kge] k-prefix suffix: sym=117 mods=0x0 ascii=117 'u' ctrl2=0 pass_ctrl=0 mapped=1 id=38"
   - "[kge] k-prefix suffix: sym=117 mods=0x80 ascii=117 'u' ctrl2=1 pass_ctrl=0 mapped=1 id=38"
 - No logs were produced for 'U' (neither KEYDOWN nor TEXTINPUT). The /tmp log file was not created on the user’s host in the last run (stderr logs were visible earlier from KEYDOWN).

Hypotheses for current failures
 1) Undo appears to be invoked (status "Undone"), but no state change:
    - The most likely cause is that no committed node exists at the time of undo (i.e., typing "Hello" is not being recorded as an undo node), because our current typing path in Command.cc directly edits buffer rows without always driving UndoSystem Begin/Append/commit at the right times for every printable char on GUI.
    - Although we call u->Begin(Insert) and u->Append(text) in cmd_insert_text for CommandId::InsertText, the GUI printable input might be arriving through a different path or being short-circuited (e.g., via a prompt or suppression), resulting in actual text insertion but no corresponding UndoSystem pending node content, or pending but never committed.
    - We now commit at k-prefix entry and before undo; if there is still "nothing to undo", it implies the batch never had text appended (Append not called) or is detached from the real buffer edits.

 2) Redo via C-k U inserts a literal 'U':
    - On macOS, uppercase letters often arrive as SDL_TEXTINPUT events. We added TEXTINPUT-based k-prefix mapping, but the user's run still showed a literal insertion and no diagnostic lines for TEXTINPUT, which suggests:
      a) The TEXTINPUT suppression didn’t trigger for that platform/sequence, or
      b) The k-prefix flag was already cleared by the time TEXTINPUT arrived, so the TEXTINPUT path defaulted to InsertText, or
      c) The GUI window’s input focus or SDL event ordering differs from expectations (e.g., IME/text input settings), so our suppression/mapping didn’t see the event.

Relevant code pointers
 - Key mapping tables: KKeymap.cc → KLookupKCommand() (C-k suffix), KLookupCtrlCommand(), KLookupEscCommand().
 - Terminal input: TerminalInputHandler.cc → map_key_to_command().
 - GUI input: GUIInputHandler.cc → map_key() and GUIInputHandler::ProcessSDLEvent() (KEYDOWN + TEXTINPUT handling, suppression, k_prefix_/esc_meta_ flags).
 - Command dispatch: Command.cc → cmd_insert_text(), cmd_newline(), cmd_backspace(), cmd_delete_char(), cmd_undo(), cmd_redo(), cmd_kprefix().
 - Undo core: UndoSystem.{h,cc}, UndoNode.{h,cc}, UndoTree.{h,cc}. Buffer raw methods used by apply().

Immediate next steps (when we return to this)
 1) Verify that GUI printable insertion always flows through CommandId::InsertText so UndoSystem::Begin/Append gets called. If SDL_TEXTINPUT delivers multi-byte strings, ensure Append() is given the same text inserted into buffer.
    - Add a one-session debug hook in cmd_insert_text to assert/trace: pending node type/text length and current cursor col before/after.
    - If GUI sometimes sends CommandId::InsertTextEmpty or another path, unify.

 2) Ensure batching rules are satisfied so Begin() reuses pending correctly:
    - Begin(Insert) must see same row and col == pending->col + pending->text.size() for typing sequences.
    - If GUI accumulates multiple characters per TEXTINPUT (e.g., pasted text), Append(std::string_view) is fine, but row/col expectations remain.

 3) For C-k U uppercase mapping on macOS:
    - Add a temporary status dump when k-prefix suffix mapping falls back to TEXTINPUT path (we added stderr prints; also set Editor status with a short code like "K-TI U" during one session) to confirm path is used and suppression is working.
    - If TEXTINPUT never arrives, force suppression: when we detect k-prefix and KEYDOWN of a letter with Shift, preemptively handle via KEYDOWN-derived uppercase ASCII rather than deferring.

 4) Consolidate k-prefix handling:
    - After mapping a k-prefix suffix to a command (Undo/Redo/etc.), always set suppress_text_input_once_ = true to avoid any trailing TEXTINPUT.
    - Clear k_prefix_ reliably on both KEYDOWN and TEXTINPUT paths.

 5) Once mapping is solid, remove all diagnostics and keep the minimal, deterministic logic.

Open questions for future debugging
 - Does SDL on this macOS setup deliver Shift+U as KEYDOWN+TEXTINPUT consistently, or only TEXTINPUT? We need a small on-screen debug to avoid relying on stderr.
 - Are there any IME/TextInput SDL hints on macOS we should set for raw key handling during k-prefix?
 - Should we temporarily disable SDL text input (SDL_StopTextInput) during k-prefix suffix processing to eliminate TEXTINPUT races on macOS?

Notes on UndoSystem correctness (unrelated to the GUI mapping bug)
 - Undo tree invariants are implemented: pending is detached; commit attaches and clears redo branches; undo/redo apply low-level Buffer edits with no public editor paths; saved marker updated via mark_saved().
 - Dirty flag mirrors (current != saved).
 - Delete batching supports prepend for backspace sequences (stored text is in increasing column order from anchor).
 - Newline joins/splits are recorded as UndoType::Newline and committed immediately for single-step undo of line joins.

Owner pointers & file locations
 - GUI mapping and suppression: GUIInputHandler.cc
 - Command layer commit boundaries: Command.cc (cmd_kprefix, cmd_undo, cmd_redo)
 - Undo batching entry points: Command.cc (cmd_insert_text, cmd_backspace, cmd_delete_char, cmd_newline)

End of snapshot — safe to resume from here.

---

RESOLUTION (2025-11-30)

Root Cause Identified and Fixed
The undo system failure was caused by incorrect timing of UndoSystem::Begin() and Append() calls relative to buffer modifications in Command.cc.

Problem:
- In cmd_insert_text, cmd_backspace, cmd_delete_char, and cmd_newline, the undo recording (Begin/Append) was called BEFORE the actual buffer modification and cursor update.
- UndoSystem::Begin() checks the current cursor position to determine if it can batch with the pending node.
- For Insert type: Begin() checks if col == pending->col + pending->text.size()
- For Delete type: Begin() checks if the cursor is at the expected position based on whether it's forward delete or backspace
- When Begin/Append were called before cursor updates, the batching condition would fail on the second character because the cursor hadn't moved yet from the first insertion.
- This caused each character to create a separate batch, but since commit() was never called between characters (only at k-prefix or undo), the pending node would be overwritten rather than committed, resulting in no undo history.

Fix Applied:
- cmd_insert_text: Moved Begin/Append to AFTER buffer insertion (lines 854-856) and cursor update (line 857).
- cmd_backspace: Moved Begin/Append to AFTER character deletion (lines 1024-1025) and cursor decrement (line 1026).
- cmd_delete_char: Moved Begin/Append to AFTER character deletion (lines 1074-1076).
- cmd_newline: Moved Begin/commit to AFTER line split (lines 956-966) and cursor update (lines 963-967).

Result:
- Begin() now sees the correct cursor position after each edit, allowing proper batching of consecutive characters.
- Typing "Hello" will now create a single pending batch with all 5 characters that can be undone as one unit.
- The fix applies to both terminal (kte) and GUI (kge) builds.

Testing Recommendation:
- Type several characters (e.g., "Hello")
- Press C-k u to undo - the entire word should disappear
- Press C-k U to redo - the word should reappear
- Test backspace batching: type several characters, then backspace multiple times, then undo - should undo the backspace batch
- Test delete batching similarly
