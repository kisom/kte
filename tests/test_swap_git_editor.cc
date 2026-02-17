#include "Test.h"

#include "Command.h"
#include "Editor.h"

#include "tests/TestHarness.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;


static void
write_file_bytes(const std::string &path, const std::string &bytes)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), (std::streamsize) bytes.size());
}


// Simulate git editor workflow: open file, edit, save, edit more, close.
// The swap file should be deleted on close, not left behind.
TEST(SwapCleanup_GitEditorWorkflow)
{
	ktet::InstallDefaultCommandsOnce();

	const fs::path xdg_root = fs::temp_directory_path() /
	                          (std::string("kte_ut_xdg_state_git_editor_") + std::to_string((int) ::getpid()));
	fs::remove_all(xdg_root);
	fs::create_directories(xdg_root);

	const char *old_xdg_p     = std::getenv("XDG_STATE_HOME");
	const std::string old_xdg = old_xdg_p ? std::string(old_xdg_p) : std::string();
	const std::string xdg_s   = xdg_root.string();
	setenv("XDG_STATE_HOME", xdg_s.c_str(), 1);

	// Simulate git's COMMIT_EDITMSG path
	const std::string path = (xdg_root / ".git" / "COMMIT_EDITMSG").string();
	fs::create_directories((xdg_root / ".git"));
	std::remove(path.c_str());
	write_file_bytes(path, "# Enter commit message\n");

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	std::string err;
	ASSERT_TRUE(ed.OpenFile(path, err));
	Buffer *b = ed.CurrentBuffer();
	ASSERT_TRUE(b != nullptr);

	// User edits the file
	ASSERT_TRUE(Execute(ed, CommandId::MoveFileStart));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "X"));
	ASSERT_TRUE(b->Dirty());

	// User saves (git will read this)
	ASSERT_TRUE(Execute(ed, CommandId::Save));
	ASSERT_TRUE(!b->Dirty());
	ed.Swap()->Flush(b);

	const std::string swp = kte::SwapManager::ComputeSwapPathForTests(*b);
	// After save, swap should be deleted
	ASSERT_TRUE(!fs::exists(swp));

	// User makes more edits (common in git editor workflow - refining message)
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "Y"));
	ASSERT_TRUE(b->Dirty());
	ed.Swap()->Flush(b);

	// Now there's a new swap file for the unsaved edits
	ASSERT_TRUE(fs::exists(swp));

	// User closes the buffer (or kte exits)
	// This simulates what happens when git is done and kte closes
	const std::size_t idx = ed.CurrentBufferIndex();
	ed.CloseBuffer(idx);

	// The swap file should be deleted on close, even though buffer was dirty
	// This prevents stale swap files when used as git editor
	ASSERT_TRUE(!fs::exists(swp));

	// Cleanup
	std::remove(path.c_str());
	if (!old_xdg.empty())
		setenv("XDG_STATE_HOME", old_xdg.c_str(), 1);
	else
		unsetenv("XDG_STATE_HOME");
	fs::remove_all(xdg_root);
}
