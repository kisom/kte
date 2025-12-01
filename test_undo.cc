#include <cassert>
#include <fstream>
#include <iostream>

#include "Buffer.h"
#include "Command.h"
#include "Editor.h"
#include "TestFrontend.h"


int
main()
{
	// Install default commands
	InstallDefaultCommands();

	Editor editor;
	TestFrontend frontend;

	// Initialize frontend
	if (!frontend.Init(editor)) {
		std::cerr << "Failed to initialize frontend\n";
		return 1;
	}

	// Create a temporary test file
	std::string err;
	const char *tmpfile = "/tmp/kte_test_undo.txt";
	{
		std::ofstream f(tmpfile);
		if (!f) {
			std::cerr << "Failed to create temp file\n";
			return 1;
		}
		f << "\n"; // Write one newline so file isn't empty
		f.close();
	}

	if (!editor.OpenFile(tmpfile, err)) {
		std::cerr << "Failed to open test file: " << err << "\n";
		return 1;
	}

	Buffer *buf = editor.CurrentBuffer();
	assert(buf != nullptr);

	// Initialize cursor to (0,0) explicitly
	buf->SetCursor(0, 0);

	std::cout << "test_undo: Testing undo/redo system\n";
	std::cout << "====================================\n\n";

	bool running = true;

	// Test 1: Insert text and verify buffer contains expected text
	std::cout << "Test 1: Insert text 'Hello'\n";
	frontend.Input().QueueText("Hello");

	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}

	assert(buf->Rows().size() >= 1);
	std::string line_after_insert = std::string(buf->Rows()[0]);
	assert(line_after_insert == "Hello");
	std::cout << "  Buffer content: '" << line_after_insert << "'\n";
	std::cout << "  ✓ Text insertion verified\n\n";

	// Test 2: Undo insertion - text should be removed
	std::cout << "Test 2: Undo insertion\n";
	frontend.Input().QueueCommand(CommandId::Undo);

	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}

	assert(buf->Rows().size() >= 1);
	std::string line_after_undo = std::string(buf->Rows()[0]);
	assert(line_after_undo == "");
	std::cout << "  Buffer content: '" << line_after_undo << "'\n";
	std::cout << "  ✓ Undo successful - text removed\n\n";

	// Test 3: Redo insertion - text should be restored
	std::cout << "Test 3: Redo insertion\n";
	frontend.Input().QueueCommand(CommandId::Redo);

	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}

	assert(buf->Rows().size() >= 1);
	std::string line_after_redo = std::string(buf->Rows()[0]);
	assert(line_after_redo == "Hello");
	std::cout << "  Buffer content: '" << line_after_redo << "'\n";
	std::cout << "  ✓ Redo successful - text restored\n\n";

	// Test 4: Branching behavior – redo is discarded after new edits
	std::cout << "Test 4: Branching behavior (redo discarded after new edits)\n";
	// Reset to empty by undoing the last redo and the original insert, then reinsert 'abc'
	// Ensure buffer is empty before starting this scenario
	frontend.Input().QueueCommand(CommandId::Undo); // undo Hello
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "");

	// Type a contiguous word 'abc' (single batch)
	frontend.Input().QueueText("abc");
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "abc");

	// Undo once – should remove the whole batch and leave empty
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "");

	// Now type new text 'X' – this should create a new branch and discard old redo chain
	frontend.Input().QueueText("X");
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "X");

	// Attempt Redo – should be a no-op (redo branch was discarded by new edit)
	frontend.Input().QueueCommand(CommandId::Redo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "X");
	// Undo and Redo along the new branch should still work
	frontend.Input().QueueCommand(CommandId::Undo);
	frontend.Input().QueueCommand(CommandId::Redo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "X");
	std::cout << "  ✓ Redo discarded after new edit; new branch undo/redo works\n\n";

	// Clear buffer state for next tests: undo to empty if needed
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "");

	// Test 5: UTF-8 insertion and undo/redo round-trip
	std::cout << "Test 5: UTF-8 insertion 'é漢' and undo/redo\n";
	const std::string utf8_text = "é漢"; // multi-byte UTF-8 (2 bytes + 3 bytes)
	frontend.Input().QueueText(utf8_text);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == utf8_text);
	// Undo should remove the entire contiguous insertion batch
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "");
	// Redo restores it
	frontend.Input().QueueCommand(CommandId::Redo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == utf8_text);
	std::cout << "  ✓ UTF-8 insert round-trips with undo/redo\n\n";

	// Clear for next test
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "");

	// Test 6: Multi-line operations (newline split and join via backspace at BOL)
	std::cout << "Test 6: Newline split and join via backspace at BOL\n";
	// Insert "ab" then newline then "cd" → expect two lines
	frontend.Input().QueueText("ab");
	frontend.Input().QueueCommand(CommandId::Newline);
	frontend.Input().QueueText("cd");
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(buf->Rows().size() >= 2);
	assert(std::string(buf->Rows()[0]) == "ab");
	assert(std::string(buf->Rows()[1]) == "cd");
	std::cout << "  ✓ Split into two lines\n";

	// Undo once – should remove "cd" insertion leaving two lines ["ab", ""] or join depending on commit
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	// Current design batches typing on the second line; after undo, the second line should exist but be empty
	assert(buf->Rows().size() >= 2);
	assert(std::string(buf->Rows()[0]) == "ab");
	assert(std::string(buf->Rows()[1]) == "");

	// Undo the newline – should rejoin to a single line "ab"
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(buf->Rows().size() >= 1);
	assert(std::string(buf->Rows()[0]) == "ab");

	// Redo twice to get back to ["ab","cd"]
	frontend.Input().QueueCommand(CommandId::Redo);
	frontend.Input().QueueCommand(CommandId::Redo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "ab");
	assert(std::string(buf->Rows()[1]) == "cd");
	std::cout << "  ✓ Newline undo/redo round-trip\n";

	// Now join via Backspace at beginning of second line
	frontend.Input().QueueCommand(CommandId::MoveDown); // ensure we're on the second line
	frontend.Input().QueueCommand(CommandId::MoveHome); // go to BOL on second line
	frontend.Input().QueueCommand(CommandId::Backspace); // join with previous line
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(buf->Rows().size() >= 1);
	assert(std::string(buf->Rows()[0]) == "abcd");
	std::cout << "  ✓ Backspace at BOL joins lines\n";

	// Undo/Redo the join
	frontend.Input().QueueCommand(CommandId::Undo);
	frontend.Input().QueueCommand(CommandId::Redo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(buf->Rows().size() >= 1);
	assert(std::string(buf->Rows()[0]) == "abcd");
	std::cout << "  ✓ Join undo/redo round-trip\n\n";

	// Test 7: Typing batching – a contiguous word undone in one step
	std::cout << "Test 7: Typing batching (single undo removes whole word)\n";
	// Clear current line first
	frontend.Input().QueueCommand(CommandId::MoveHome);
	frontend.Input().QueueCommand(CommandId::KillToEOL);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]).empty());
	// Type a word and verify one undo clears it
	frontend.Input().QueueText("hello");
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "hello");
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]).empty());
	frontend.Input().QueueCommand(CommandId::Redo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "hello");
	std::cout << "  ✓ Contiguous typing batched into single undo step\n\n";

	// Test 8: Forward delete batching at a fixed anchor column
	std::cout << "Test 8: Forward delete batching at fixed anchor (DeleteChar)\n";
	// Prepare line content
	frontend.Input().QueueCommand(CommandId::MoveHome);
	frontend.Input().QueueCommand(CommandId::KillToEOL);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	frontend.Input().QueueText("abcdef");
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	// Ensure cursor at anchor column 0
	frontend.Input().QueueCommand(CommandId::MoveHome);
	// Delete three chars at cursor; should batch into one Delete node
	frontend.Input().QueueCommand(CommandId::DeleteChar, "", 3);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "def");
	// Single undo should restore the entire deleted run
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "abcdef");
	// Redo should remove the same run again
	frontend.Input().QueueCommand(CommandId::Redo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "def");
	std::cout << "  ✓ Forward delete batched and undo/redo round-trips\n\n";

	// Test 9: Backspace batching with prepend rule (cursor moves left)
	std::cout << "Test 9: Backspace batching with prepend rule\n";
	// Restore to full string then backspace a run
	frontend.Input().QueueCommand(CommandId::Undo); // bring back to "abcdef"
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "abcdef");
	// Move to end and backspace three characters; should batch into one Delete node
	frontend.Input().QueueCommand(CommandId::MoveEnd);
	frontend.Input().QueueCommand(CommandId::Backspace, "", 3);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "abc");
	// Single undo restores the deleted run
	frontend.Input().QueueCommand(CommandId::Undo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "abcdef");
	// Redo removes it again
	frontend.Input().QueueCommand(CommandId::Redo);
	while (!frontend.Input().IsEmpty() && running) {
		frontend.Step(editor, running);
	}
	assert(std::string(buf->Rows()[0]) == "abc");
	std::cout << "  ✓ Backspace run batched and undo/redo round-trips\n\n";

	frontend.Shutdown();

	std::cout << "====================================\n";
	std::cout << "All tests passed!\n";

	return 0;
}
