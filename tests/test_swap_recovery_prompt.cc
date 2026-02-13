#include "Test.h"

#include "Buffer.h"
#include "Command.h"
#include "Editor.h"
#include "Swap.h"

#include "tests/TestHarness.h" // for ktet::InstallDefaultCommandsOnce

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>


namespace {
static void
write_file_bytes(const std::string &path, const std::string &bytes)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), (std::streamsize) bytes.size());
}


static std::string
read_file_bytes(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}


static std::string
buffer_bytes_via_views(const Buffer &b)
{
	const auto &rows = b.Rows();
	std::string out;
	for (std::size_t i = 0; i < rows.size(); i++) {
		auto v = b.GetLineView(i);
		out.append(v.data(), v.size());
	}
	return out;
}


struct ScopedXdgStateHome {
	std::string old;
	bool had{false};


	explicit ScopedXdgStateHome(const std::string &p)
	{
		const char *old_p = std::getenv("XDG_STATE_HOME");
		had               = (old_p && *old_p);
		old               = old_p ? std::string(old_p) : std::string();
		setenv("XDG_STATE_HOME", p.c_str(), 1);
	}


	~ScopedXdgStateHome()
	{
		if (had && !old.empty()) {
			setenv("XDG_STATE_HOME", old.c_str(), 1);
		} else {
			unsetenv("XDG_STATE_HOME");
		}
	}
};
} // namespace


TEST (SwapRecoveryPrompt_Recover_ReplaysSwap)
{
	ktet::InstallDefaultCommandsOnce();

	const std::filesystem::path xdg_root = std::filesystem::temp_directory_path() /
	                                       (std::string("kte_ut_xdg_state_recover_") +
	                                        std::to_string((int) ::getpid()));
	std::filesystem::remove_all(xdg_root);
	std::filesystem::create_directories(xdg_root);
	const ScopedXdgStateHome scoped(xdg_root.string());

	const std::filesystem::path work = xdg_root / "work";
	std::filesystem::create_directories(work);
	const std::string file_path = (work / "recover.txt").string();
	write_file_bytes(file_path, "base\nline2\n");

	// Create a swap journal with unsaved edits.
	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(file_path, err));
	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));
	b.insert_text(0, 0, std::string("X"));
	b.insert_text(1, 0, std::string("ZZ"));
	sm.Flush(&b);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	const std::string expected  = buffer_bytes_via_views(b);
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b);

	// Now attempt to open via Editor deferred-open; this should trigger a recovery prompt.
	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(b.Filename());
	ASSERT_EQ(ed.ProcessPendingOpens(), false);
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::RecoverOrDiscard);
	ASSERT_EQ(ed.PromptActive(), true);

	// Answer 'y' to recover.
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "y"));
	ASSERT_TRUE(Execute(ed, CommandId::Newline));
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::None);
	ASSERT_EQ(ed.PromptActive(), false);
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_EQ(buffer_bytes_via_views(*ed.CurrentBuffer()), expected);
	ASSERT_EQ(ed.CurrentBuffer()->Dirty(), true);
	ASSERT_TRUE(std::filesystem::exists(swap_path));

	std::remove(file_path.c_str());
	std::remove(swap_path.c_str());
	std::filesystem::remove_all(xdg_root);
}


TEST (SwapRecoveryPrompt_Discard_DeletesSwapAndOpensClean)
{
	ktet::InstallDefaultCommandsOnce();

	const std::filesystem::path xdg_root = std::filesystem::temp_directory_path() /
	                                       (std::string("kte_ut_xdg_state_discard_") +
	                                        std::to_string((int) ::getpid()));
	std::filesystem::remove_all(xdg_root);
	std::filesystem::create_directories(xdg_root);
	const ScopedXdgStateHome scoped(xdg_root.string());

	const std::filesystem::path work = xdg_root / "work";
	std::filesystem::create_directories(work);
	const std::string file_path = (work / "discard.txt").string();
	write_file_bytes(file_path, "base\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(file_path, err));
	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));
	b.insert_text(0, 0, std::string("X"));
	sm.Flush(&b);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b);
	ASSERT_TRUE(std::filesystem::exists(swap_path));

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(b.Filename());
	ASSERT_EQ(ed.ProcessPendingOpens(), false);
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::RecoverOrDiscard);
	ASSERT_EQ(ed.PromptActive(), true);

	// Default answer (empty) is 'no' => discard.
	ASSERT_TRUE(Execute(ed, CommandId::Newline));
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::None);
	ASSERT_EQ(ed.PromptActive(), false);
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_EQ(buffer_bytes_via_views(*ed.CurrentBuffer()), read_file_bytes(b.Filename()));
	ASSERT_EQ(ed.CurrentBuffer()->Dirty(), false);
	ASSERT_EQ(std::filesystem::exists(swap_path), false);

	std::remove(file_path.c_str());
	std::filesystem::remove_all(xdg_root);
}


TEST (SwapRecoveryPrompt_Cancel_AbortsOpen)
{
	ktet::InstallDefaultCommandsOnce();

	const std::filesystem::path xdg_root = std::filesystem::temp_directory_path() /
	                                       (std::string("kte_ut_xdg_state_cancel_") +
	                                        std::to_string((int) ::getpid()));
	std::filesystem::remove_all(xdg_root);
	std::filesystem::create_directories(xdg_root);
	const ScopedXdgStateHome scoped(xdg_root.string());

	const std::filesystem::path work = xdg_root / "work";
	std::filesystem::create_directories(work);
	const std::string file_path = (work / "cancel.txt").string();
	write_file_bytes(file_path, "base\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(file_path, err));
	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));
	b.insert_text(0, 0, std::string("X"));
	sm.Flush(&b);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b);

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(b.Filename());
	ASSERT_EQ(ed.ProcessPendingOpens(), false);
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::RecoverOrDiscard);
	ASSERT_EQ(ed.PromptActive(), true);

	// Cancel the prompt (C-g / Refresh).
	ASSERT_TRUE(Execute(ed, CommandId::Refresh));
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::None);
	ASSERT_EQ(ed.PromptActive(), false);
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_EQ(ed.CurrentBuffer()->Filename().empty(), true);
	ASSERT_TRUE(std::filesystem::exists(swap_path));

	std::remove(file_path.c_str());
	std::remove(swap_path.c_str());
	std::filesystem::remove_all(xdg_root);
}


TEST (SwapRecoveryPrompt_CorruptSwap_OffersDelete)
{
	ktet::InstallDefaultCommandsOnce();

	const std::filesystem::path xdg_root = std::filesystem::temp_directory_path() /
	                                       (std::string("kte_ut_xdg_state_corrupt_") +
	                                        std::to_string((int) ::getpid()));
	std::filesystem::remove_all(xdg_root);
	std::filesystem::create_directories(xdg_root);
	const ScopedXdgStateHome scoped(xdg_root.string());

	const std::filesystem::path work = xdg_root / "work";
	std::filesystem::create_directories(work);
	const std::string file_path = (work / "corrupt.txt").string();
	write_file_bytes(file_path, "base\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(file_path, err));
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);

	// Write a corrupt swap file at the expected location.
	try {
		std::filesystem::create_directories(std::filesystem::path(swap_path).parent_path());
	} catch (...) {
		// ignore
	}
	write_file_bytes(swap_path, "x");
	ASSERT_TRUE(std::filesystem::exists(swap_path));

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(b.Filename());
	ASSERT_EQ(ed.ProcessPendingOpens(), false);
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::DeleteCorruptSwap);
	ASSERT_EQ(ed.PromptActive(), true);

	// Answer 'y' to delete the corrupt swap and proceed.
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "y"));
	ASSERT_TRUE(Execute(ed, CommandId::Newline));
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::None);
	ASSERT_EQ(ed.PromptActive(), false);
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_EQ(buffer_bytes_via_views(*ed.CurrentBuffer()), read_file_bytes(b.Filename()));
	ASSERT_EQ(std::filesystem::exists(swap_path), false);

	std::remove(file_path.c_str());
	std::filesystem::remove_all(xdg_root);
}