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


// RAII helper to set XDG_STATE_HOME for the duration of a test and clean up.
struct XdgStateGuard {
	fs::path root;
	std::string old_xdg;
	bool had_old;

	explicit XdgStateGuard(const std::string &suffix)
	{
		root = fs::temp_directory_path() /
		       (std::string("kte_ut_xdg_") + suffix + "_" + std::to_string((int) ::getpid()));
		fs::remove_all(root);
		fs::create_directories(root);

		const char *p = std::getenv("XDG_STATE_HOME");
		had_old       = (p != nullptr);
		if (p)
			old_xdg = p;
		setenv("XDG_STATE_HOME", root.string().c_str(), 1);
	}

	~XdgStateGuard()
	{
		if (had_old)
			setenv("XDG_STATE_HOME", old_xdg.c_str(), 1);
		else
			unsetenv("XDG_STATE_HOME");
		fs::remove_all(root);
	}
};


TEST(SwapCleanup_SaveAndQuit)
{
	ktet::InstallDefaultCommandsOnce();
	XdgStateGuard xdg("save_quit");

	const std::string path = (xdg.root / "work" / "file.txt").string();
	fs::create_directories(xdg.root / "work");
	write_file_bytes(path, "hello\n");

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	std::string err;
	ASSERT_TRUE(ed.OpenFile(path, err));
	Buffer *b = ed.CurrentBuffer();
	ASSERT_TRUE(b != nullptr);

	// Edit to create swap file
	ASSERT_TRUE(Execute(ed, CommandId::MoveFileStart));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "Z"));
	ASSERT_TRUE(b->Dirty());

	ed.Swap()->Flush(b);
	const std::string swp = kte::SwapManager::ComputeSwapPathForTests(*b);
	ASSERT_TRUE(fs::exists(swp));

	// Save-and-quit should clean up the swap file
	ASSERT_TRUE(Execute(ed, CommandId::SaveAndQuit));
	ed.Swap()->Flush(b);
	ASSERT_TRUE(!fs::exists(swp));

	// Cleanup
	std::remove(path.c_str());
}


TEST(SwapCleanup_EditorReset)
{
	ktet::InstallDefaultCommandsOnce();
	XdgStateGuard xdg("editor_reset");

	const std::string path = (xdg.root / "work" / "file.txt").string();
	fs::create_directories(xdg.root / "work");
	write_file_bytes(path, "hello\n");

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	std::string err;
	ASSERT_TRUE(ed.OpenFile(path, err));
	Buffer *b = ed.CurrentBuffer();
	ASSERT_TRUE(b != nullptr);

	// Edit to create swap file
	ASSERT_TRUE(Execute(ed, CommandId::MoveFileStart));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "W"));
	ASSERT_TRUE(b->Dirty());

	ed.Swap()->Flush(b);
	const std::string swp = kte::SwapManager::ComputeSwapPathForTests(*b);
	ASSERT_TRUE(fs::exists(swp));

	// Reset (simulates clean editor exit) should remove swap files
	ed.Reset();
	ASSERT_TRUE(!fs::exists(swp));

	// Cleanup
	std::remove(path.c_str());
}
