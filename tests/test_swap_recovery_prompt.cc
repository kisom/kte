#include "Test.h"

#include "Buffer.h"
#include "Command.h"
#include "Editor.h"
#include "Swap.h"

#include "tests/TestHarness.h" // for ktet::InstallDefaultCommandsOnce

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
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
	const auto &rows = b.LinesForTests();
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


TEST(SwapRecoveryPrompt_Recover_ReplaysSwap)
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


TEST(SwapRecoveryPrompt_Discard_DeletesSwapAndOpensClean)
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

	// An explicit 'n' discards. (An empty answer cancels instead; see
	// SwapRecoveryPrompt_EmptyAnswer_KeepsSwap.)
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "n"));
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


TEST(SwapRecoveryPrompt_Cancel_AbortsOpen)
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


TEST(SwapRecoveryPrompt_CorruptSwap_OffersDelete)
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


// Declining to delete an unreadable swap keeps it aside as ".corrupt" so the
// new session journals into a fresh file instead of behind the bad data.
TEST(SwapRecoveryPrompt_CorruptSwap_Decline_KeepsAsideAndStartsFresh)
{
	ktet::InstallDefaultCommandsOnce();

	const std::filesystem::path xdg_root = std::filesystem::temp_directory_path() /
	                                       (std::string("kte_ut_xdg_state_corrupt_keep_") +
	                                        std::to_string((int) ::getpid()));
	std::filesystem::remove_all(xdg_root);
	std::filesystem::create_directories(xdg_root);
	const ScopedXdgStateHome scoped(xdg_root.string());

	const std::filesystem::path work = xdg_root / "work";
	std::filesystem::create_directories(work);
	const std::string file_path = (work / "corrupt_keep.txt").string();
	write_file_bytes(file_path, "base\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(file_path, err));
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	std::filesystem::create_directories(std::filesystem::path(swap_path).parent_path());
	write_file_bytes(swap_path, std::string(80, 'z')); // long enough to be "trusted", bad magic

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(b.Filename());
	ASSERT_EQ(ed.ProcessPendingOpens(), false);
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::DeleteCorruptSwap);

	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "n"));
	ASSERT_TRUE(Execute(ed, CommandId::Newline));
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::None);
	ASSERT_TRUE(std::filesystem::exists(swap_path + ".corrupt"));

	// New edits journal into a fresh, replayable file.
	Buffer *cur = ed.CurrentBuffer();
	ASSERT_TRUE(cur != nullptr);
	cur->insert_text(0, 0, std::string("X"));
	ed.Swap()->Flush(cur);

	Buffer check;
	ASSERT_TRUE(check.OpenFromFile(file_path, err));
	std::string rerr;
	ASSERT_TRUE(kte::SwapManager::ReplayFile(check, swap_path, rerr));
	ASSERT_EQ(check.GetLineString(0), std::string("Xbase"));

	std::remove(file_path.c_str());
	std::filesystem::remove_all(xdg_root);
}


namespace {
// Journal for `file_path` holding the edits in `edit`, via a throwaway manager.
static std::string
make_journal(const std::string &file_path, void (*edit)(Buffer &), std::string &expected)
{
	Buffer b;
	std::string err;
	(void) b.OpenFromFile(file_path, err);
	std::remove(kte::SwapManager::ComputeSwapPathForTests(b).c_str());
	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));
	edit(b);
	sm.Flush(&b);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	expected                    = buffer_bytes_via_views(b);
	// Keep the journal as a crash would leave it (Detach would checkpoint).
	const std::string bytes = read_file_bytes(swap_path);
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b, true);
	write_file_bytes(swap_path, bytes);
	return swap_path;
}


struct XdgSandbox {
	std::filesystem::path root;
	std::unique_ptr<ScopedXdgStateHome> scoped;


	explicit XdgSandbox(const std::string &tag)
	{
		root = std::filesystem::temp_directory_path() /
		       (std::string("kte_ut_xdg_") + tag + "_" + std::to_string((int) ::getpid()));
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root / "work");
		scoped = std::make_unique<ScopedXdgStateHome>(root.string());
	}


	~XdgSandbox()
	{
		scoped.reset();
		std::filesystem::remove_all(root);
	}
};


static void
answer(Editor &ed, const char *yn)
{
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, yn));
	ASSERT_TRUE(Execute(ed, CommandId::Newline));
}
} // namespace


// Recovery must work when another named buffer is current (the AddBuffer
// path): opening used to delete the very swap file it was about to replay.
TEST(SwapRecoveryPrompt_Recover_WithAnotherNamedBufferOpen)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("recover_named");
	const std::string other = (sb.root / "work" / "other.txt").string();
	const std::string file  = (sb.root / "work" / "r.txt").string();
	write_file_bytes(other, "other\n");
	write_file_bytes(file, "base\n");
	std::string expected;
	const std::string swp = make_journal(file, [](Buffer &b) {
		b.insert_text(0, 0, std::string("A"));
	}, expected);

	Editor ed;
	ed.SetDimensions(24, 80);
	std::string err;
	ASSERT_TRUE(ed.OpenFile(other, err));
	ed.RequestOpenFile(file);
	(void) ed.ProcessPendingOpens();
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::RecoverOrDiscard);
	answer(ed, "y");
	ASSERT_EQ(buffer_bytes_via_views(*ed.CurrentBuffer()), expected);
	ASSERT_TRUE(std::filesystem::exists(swp));
}


// After recovering from a journal whose last record is torn, the torn bytes
// are dropped, so records from the recovered session stay replayable.
TEST(SwapRecoveryPrompt_Recover_TornTail_JournalStaysReplayable)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("recover_torn");
	const std::string file = (sb.root / "work" / "t.txt").string();
	write_file_bytes(file, "base\n");
	std::string expected;
	const std::string swp = make_journal(file, [](Buffer &b) {
		b.insert_text(0, 0, std::string("A"));
	}, expected);
	write_file_bytes(swp, read_file_bytes(swp) + std::string("\x01\x20\x00\x00zz", 6)); // torn record

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(file);
	(void) ed.ProcessPendingOpens();
	answer(ed, "y");
	Buffer *cur = ed.CurrentBuffer();
	ASSERT_EQ(buffer_bytes_via_views(*cur), std::string("Abase\n"));
	cur->insert_text(0, 1, std::string("Z"));
	ed.Swap()->Flush(cur);

	Buffer check;
	std::string err, rerr;
	ASSERT_TRUE(check.OpenFromFile(file, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(check, swp, rerr));
	ASSERT_EQ(buffer_bytes_via_views(check), std::string("AZbase\n"));
}


// Opening a file that is already open switches to its buffer instead of
// creating a second buffer sharing the same swap journal.
TEST(SwapRecoveryPrompt_OpenTwice_SwitchesToExisting)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("open_twice");
	const std::string file  = (sb.root / "work" / "d.txt").string();
	const std::string other = (sb.root / "work" / "o.txt").string();
	write_file_bytes(file, "x\n");
	write_file_bytes(other, "y\n");

	Editor ed;
	ed.SetDimensions(24, 80);
	std::string err;
	ASSERT_TRUE(ed.OpenFile(file, err));
	ASSERT_TRUE(ed.OpenFile(other, err));
	const std::size_t n = ed.Buffers().size();
	ASSERT_TRUE(ed.OpenFile(file, err));
	ASSERT_EQ(ed.Buffers().size(), n);
	ASSERT_EQ(ed.CurrentBuffer()->Filename(), std::filesystem::canonical(file).string());
	ed.RequestOpenFile(other);
	(void) ed.ProcessPendingOpens();
	ASSERT_EQ(ed.Buffers().size(), n);
	ASSERT_EQ(ed.CurrentBuffer()->Filename(), std::filesystem::canonical(other).string());
}


// Distinct paths must not map to the same swap file.
TEST(SwapPath_EncodingIsOneToOne)
{
	ASSERT_TRUE(kte::SwapManager::ComputeSwapPathForFilename("/x/a!b") !=
	            kte::SwapManager::ComputeSwapPathForFilename("/x/a/b"));
	ASSERT_TRUE(kte::SwapManager::ComputeSwapPathForFilename("/x/a%21b") !=
	            kte::SwapManager::ComputeSwapPathForFilename("/x/a!b"));
}


// Reload restarts the journal: edits discarded by the reload must not come
// back through crash recovery.
TEST(SwapJournal_Reload_ResetsJournal)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("reload");
	const std::string file = (sb.root / "work" / "rl.txt").string();
	write_file_bytes(file, "hello\nworld\n");

	Editor ed;
	ed.SetDimensions(24, 80);
	std::string err;
	ASSERT_TRUE(ed.OpenFile(file, err));
	Buffer *cur = ed.CurrentBuffer();
	cur->SetCursor(0, 0);
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "XXXX"));
	// The buffer is dirty: the first reload only asks for confirmation.
	ASSERT_TRUE(Execute(ed, CommandId::ReloadBuffer));
	ASSERT_TRUE(ed.CurrentBuffer()->Dirty());
	ASSERT_TRUE(Execute(ed, CommandId::ReloadBuffer));
	cur = ed.CurrentBuffer();
	cur->SetCursor(0, 1);
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "YY"));
	ed.Swap()->Flush(cur);
	const std::string live = buffer_bytes_via_views(*cur);
	ASSERT_EQ(live, std::string("hello\nYYworld\n"));

	Buffer check;
	std::string rerr;
	ASSERT_TRUE(check.OpenFromFile(file, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(check, kte::SwapManager::ComputeSwapPathForTests(*cur), rerr));
	ASSERT_EQ(buffer_bytes_via_views(check), live);
}


// A journal whose base file changed on disk afterwards is not replayed onto
// the new content; the user is offered to delete it instead.
TEST(SwapRecoveryPrompt_StaleJournal_NotReplayed)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("stale");
	const std::string file = (sb.root / "work" / "s.txt").string();
	write_file_bytes(file, "base\n");
	std::string expected;
	(void) make_journal(file, [](Buffer &b) {
		b.insert_text(0, 0, std::string("A"));
	}, expected);
	write_file_bytes(file, "completely different content\n"); // changed after the crash

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(file);
	(void) ed.ProcessPendingOpens();
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::DeleteCorruptSwap);
	answer(ed, "y");
	ASSERT_EQ(buffer_bytes_via_views(*ed.CurrentBuffer()), std::string("completely different content\n"));
}


// A journal held by another live session is neither replayed, removed nor
// written to by a second session opening the same file.
TEST(SwapRecoveryPrompt_LiveJournalOfOtherSession_LeftAlone)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("live");
	const std::string file = (sb.root / "work" / "l.txt").string();
	write_file_bytes(file, "base\n");

	// "Other process": a session with the journal open (and locked).
	Buffer other;
	std::string err;
	ASSERT_TRUE(other.OpenFromFile(file, err));
	kte::SwapManager other_sm;
	other_sm.Attach(&other);
	other.SetSwapRecorder(other_sm.RecorderFor(&other));
	other.insert_text(0, 0, std::string("O"));
	other_sm.Flush(&other);
	const std::string swp = kte::SwapManager::ComputeSwapPathForTests(other);
	ASSERT_TRUE(kte::SwapManager::JournalInUse(swp));
	const std::string before = read_file_bytes(swp);

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(file);
	(void) ed.ProcessPendingOpens();
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::None);
	ASSERT_EQ(buffer_bytes_via_views(*ed.CurrentBuffer()), std::string("base\n"));
	ed.CurrentBuffer()->insert_text(0, 0, std::string("X"));
	ed.Swap()->Flush(ed.CurrentBuffer());
	ASSERT_EQ(read_file_bytes(swp), before);

	other.SetSwapRecorder(nullptr);
	other_sm.Detach(&other, true);
}


// A session locked out of a journal (another session owns it) must not
// delete it when it closes or saves the buffer.
TEST(SwapJournal_SecondSession_DoesNotDeleteOwnersJournal)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("owner");
	const std::string file = (sb.root / "work" / "g.txt").string();
	write_file_bytes(file, "base\n");

	Editor a;
	a.SetDimensions(24, 80);
	std::string err;
	ASSERT_TRUE(a.OpenFile(file, err));
	a.CurrentBuffer()->insert_text(0, 0, std::string("A"));
	a.Swap()->Flush(a.CurrentBuffer());
	const std::string swp = kte::SwapManager::ComputeSwapPathForTests(*a.CurrentBuffer());
	ASSERT_TRUE(std::filesystem::exists(swp));

	{
		Editor b;
		b.SetDimensions(24, 80);
		b.AddBuffer(Buffer());
		b.RequestOpenFile(file);
		(void) b.ProcessPendingOpens();
		b.CurrentBuffer()->insert_text(0, 0, std::string("B"));
		b.Swap()->Flush(b.CurrentBuffer());
		ASSERT_TRUE(b.CloseBuffer(b.CurrentBufferIndex()));
	}
	ASSERT_TRUE(std::filesystem::exists(swp));

	// A's journal still replays to A's content.
	Buffer check;
	std::string rerr;
	ASSERT_TRUE(check.OpenFromFile(file, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(check, swp, rerr));
	ASSERT_EQ(buffer_bytes_via_views(check), std::string("Abase\n"));
}


// Enter alone (or any key other than y/n) at the recovery prompt cancels and
// keeps the journal: it used to count as "no" and delete it.
TEST(SwapRecoveryPrompt_EmptyAnswer_KeepsSwap)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("empty_answer");
	const std::string file = (sb.root / "work" / "e.txt").string();
	write_file_bytes(file, "base\n");
	std::string expected;
	const std::string swp = make_journal(file, [](Buffer &b) {
		b.insert_text(0, 0, std::string("A"));
	}, expected);

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(file);
	(void) ed.ProcessPendingOpens();
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::RecoverOrDiscard);
	answer(ed, "x");
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::None);
	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_TRUE(std::filesystem::exists(swp));
}


// A timestamp-only change (touch, git checkout back and forth) does not make
// a journal stale: its base content is unchanged.
TEST(SwapRecoveryPrompt_TouchedFile_StillRecoverable)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("touched");
	const std::string file = (sb.root / "work" / "t2.txt").string();
	write_file_bytes(file, "base\n");
	std::string expected;
	(void) make_journal(file, [](Buffer &b) {
		b.insert_text(0, 0, std::string("A"));
	}, expected);
	std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::hours(1));

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(file);
	(void) ed.ProcessPendingOpens();
	ASSERT_EQ(ed.PendingRecoveryPrompt(), Editor::RecoveryPromptKind::RecoverOrDiscard);
	answer(ed, "y");
	ASSERT_EQ(buffer_bytes_via_views(*ed.CurrentBuffer()), expected);
}


// A recovered buffer stays modified even after an edit and its undo: its
// content differs from the file on disk in every undo state.
TEST(SwapRecoveryPrompt_Recovered_StaysDirtyAfterUndo)
{
	ktet::InstallDefaultCommandsOnce();
	XdgSandbox sb("recovered_dirty");
	const std::string file = (sb.root / "work" / "rd.txt").string();
	write_file_bytes(file, "base\n");
	std::string expected;
	(void) make_journal(file, [](Buffer &b) {
		b.insert_text(0, 0, std::string("IMPORTANT "));
	}, expected);

	Editor ed;
	ed.SetDimensions(24, 80);
	ed.AddBuffer(Buffer());
	ed.RequestOpenFile(file);
	(void) ed.ProcessPendingOpens();
	answer(ed, "y");
	ASSERT_TRUE(ed.CurrentBuffer()->Dirty());
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "a"));
	ASSERT_TRUE(Execute(ed, CommandId::Undo));
	ASSERT_TRUE(ed.CurrentBuffer()->Dirty());
}
