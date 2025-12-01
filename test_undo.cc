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

	frontend.Shutdown();

	std::cout << "====================================\n";
	std::cout << "All tests passed!\n";

	return 0;
}
