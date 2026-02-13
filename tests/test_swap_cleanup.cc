#include "Test.h"

#include "Command.h"
#include "Editor.h"

#include "tests/TestHarness.h" // for ktet::InstallDefaultCommandsOnce

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


TEST (SwapCleanup_ResetJournalOnSave)
{
	ktet::InstallDefaultCommandsOnce();

	const fs::path xdg_root = fs::temp_directory_path() /
	                          (std::string("kte_ut_xdg_state_swap_cleanup_") + std::to_string((int) ::getpid()));
	fs::remove_all(xdg_root);
	fs::create_directories(xdg_root);

	const char *old_xdg_p     = std::getenv("XDG_STATE_HOME");
	const std::string old_xdg = old_xdg_p ? std::string(old_xdg_p) : std::string();
	const std::string xdg_s   = xdg_root.string();
	setenv("XDG_STATE_HOME", xdg_s.c_str(), 1);

	const std::string path = (xdg_root / "work" / "file.txt").string();
	fs::create_directories((xdg_root / "work"));
	std::remove(path.c_str());
	write_file_bytes(path, "base\n");

	Editor ed;
	ed.SetDimensions(24, 80);
	// Seed scratch buffer so OpenFile can reuse it.
	ed.AddBuffer(Buffer());
	std::string err;
	ASSERT_TRUE(ed.OpenFile(path, err));
	Buffer *b = ed.CurrentBuffer();
	ASSERT_TRUE(b != nullptr);

	// Edit to ensure swap is created.
	ASSERT_TRUE(Execute(ed, CommandId::MoveFileStart));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "X"));
	ASSERT_TRUE(b->Dirty());

	ed.Swap()->Flush(b);
	const std::string swp = kte::SwapManager::ComputeSwapPathForTests(*b);
	ASSERT_TRUE(fs::exists(swp));

	// Save should reset/delete the journal.
	ASSERT_TRUE(Execute(ed, CommandId::Save));
	ed.Swap()->Flush(b);
	ASSERT_TRUE(!fs::exists(swp));

	// Subsequent edits should recreate a fresh swap.
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "Y"));
	ed.Swap()->Flush(b);
	ASSERT_TRUE(fs::exists(swp));

	// Cleanup.
	ed.Swap()->Detach(b);
	std::remove(path.c_str());
	std::remove(swp.c_str());
	if (!old_xdg.empty())
		setenv("XDG_STATE_HOME", old_xdg.c_str(), 1);
	else
		unsetenv("XDG_STATE_HOME");
	fs::remove_all(xdg_root);
}


TEST (SwapCleanup_PruneSwapDir_ByAge)
{
	const fs::path xdg_root = fs::temp_directory_path() /
	                          (std::string("kte_ut_xdg_state_swap_prune_") + std::to_string((int) ::getpid()));
	fs::remove_all(xdg_root);
	fs::create_directories(xdg_root);

	const char *old_xdg_p     = std::getenv("XDG_STATE_HOME");
	const std::string old_xdg = old_xdg_p ? std::string(old_xdg_p) : std::string();
	const std::string xdg_s   = xdg_root.string();
	setenv("XDG_STATE_HOME", xdg_s.c_str(), 1);

	const fs::path swapdir = xdg_root / "kte" / "swap";
	fs::create_directories(swapdir);
	const fs::path oldp = swapdir / "old.swp";
	const fs::path newp = swapdir / "new.swp";
	const fs::path keep = swapdir / "note.txt";
	write_file_bytes(oldp.string(), "x");
	write_file_bytes(newp.string(), "y");
	write_file_bytes(keep.string(), "z");

	// Make old.swp look old (2 days ago) and new.swp recent.
	std::error_code ec;
	fs::last_write_time(oldp, fs::file_time_type::clock::now() - std::chrono::hours(48), ec);
	fs::last_write_time(newp, fs::file_time_type::clock::now(), ec);

	kte::SwapManager sm;
	kte::SwapConfig cfg;
	cfg.prune_on_startup   = false;
	cfg.prune_max_age_days = 1;
	cfg.prune_max_files    = 0; // disable count-based pruning for this test
	sm.SetConfig(cfg);
	sm.PruneSwapDir();

	ASSERT_TRUE(!fs::exists(oldp));
	ASSERT_TRUE(fs::exists(newp));
	ASSERT_TRUE(fs::exists(keep));

	// Cleanup.
	std::remove(newp.string().c_str());
	std::remove(keep.string().c_str());
	if (!old_xdg.empty())
		setenv("XDG_STATE_HOME", old_xdg.c_str(), 1);
	else
		unsetenv("XDG_STATE_HOME");
	fs::remove_all(xdg_root);
}