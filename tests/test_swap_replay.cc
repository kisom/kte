#include "Test.h"

#include "Buffer.h"
#include "Swap.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <thread>
#include <string>
#include <vector>


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


static std::vector<std::uint8_t>
record_types_from_bytes(const std::string &bytes)
{
	std::vector<std::uint8_t> types;
	if (bytes.size() < 64)
		return types;
	std::size_t off = 64;
	while (off < bytes.size()) {
		if (bytes.size() - off < 8)
			break;
		const std::uint8_t type = static_cast<std::uint8_t>(bytes[off + 0]);
		const std::uint32_t len = (std::uint32_t) static_cast<std::uint8_t>(bytes[off + 1]) |
		                          ((std::uint32_t) static_cast<std::uint8_t>(bytes[off + 2]) << 8) |
		                          ((std::uint32_t) static_cast<std::uint8_t>(bytes[off + 3]) << 16);
		const std::size_t crc_off = off + 4 + (std::size_t) len;
		if (crc_off + 4 > bytes.size())
			break;
		types.push_back(type);
		off = crc_off + 4;
	}
	return types;
}


TEST(SwapReplay_RecordFlushReopenReplay_ExactBytesMatch)
{
	const std::string path = "./.kte_ut_swap_replay_1.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "base\nline2\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));

	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));

	// Edits (no save): swap should capture these.
	b.insert_text(0, 0, std::string("X")); // Xbase\nline2\n
	b.delete_text(1, 1, 2); // delete "in" from "line2"
	b.split_line(0, 3); // Xba\nse...
	b.join_lines(0); // join back
	b.insert_text(1, 0, std::string("ZZ")); // insert at start of line2
	b.delete_text(0, 0, 1); // delete leading X

	sm.Flush(&b);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	const std::string expected  = buffer_bytes_via_views(b);

	// Close journal before replaying (for determinism)
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b);

	Buffer b2;
	ASSERT_TRUE(b2.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b2, swap_path, err));
	ASSERT_EQ(buffer_bytes_via_views(b2), expected);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


// A crash mid-append leaves a torn final record. Replay keeps every complete
// record before it (that tail is exactly what the journal exists to save),
// succeeds, and reports the incomplete record through err.
TEST(SwapReplay_TruncatedLog_RecoversPrefix)
{
	const std::string path = "./.kte_ut_swap_replay_2.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "hello\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));

	// Start from a fresh journal even if an earlier failed run left one.
	std::remove(kte::SwapManager::ComputeSwapPathForTests(b).c_str());
	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));
	b.insert_text(0, 0, std::string("X"));
	b.insert_text(0, 1, std::string("Y"));
	sm.Flush(&b);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b);

	const std::string bytes = read_file_bytes(swap_path);
	ASSERT_TRUE(bytes.size() > 70); // header + at least one record

	const std::string trunc_path = swap_path + ".trunc";
	write_file_bytes(trunc_path, bytes.substr(0, bytes.size() - 1));

	Buffer b2;
	ASSERT_TRUE(b2.OpenFromFile(path, err));
	std::string rerr;
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b2, trunc_path, rerr));
	ASSERT_TRUE(rerr.find("incomplete record") != std::string::npos);
	// Detach appended a final checkpoint; that is the torn record, so both
	// inserts before it are recovered.
	ASSERT_EQ(b2.GetLineString(0), std::string("XYhello"));

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
	std::remove(trunc_path.c_str());
}


TEST(SwapReplay_Checkpoint_Midstream_ExactBytesMatch)
{
	const std::string path = "./.kte_ut_swap_replay_chkpt_1.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "base\nline2\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));

	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));

	// Some edits, then an explicit checkpoint, then more edits.
	b.insert_text(0, 0, std::string("X"));
	sm.Checkpoint(&b);
	b.insert_text(1, 0, std::string("ZZ"));
	b.delete_text(0, 0, 1);

	sm.Flush(&b);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	const std::string expected  = buffer_bytes_via_views(b);

	b.SetSwapRecorder(nullptr);
	sm.Detach(&b);

	Buffer b2;
	ASSERT_TRUE(b2.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b2, swap_path, err));
	ASSERT_EQ(buffer_bytes_via_views(b2), expected);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST(SwapCompaction_RewritesToSingleCheckpoint)
{
	const std::string path = "./.kte_ut_swap_compact_1.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "base\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));

	kte::SwapManager sm;
	kte::SwapConfig cfg;
	cfg.checkpoint_bytes       = 0;
	cfg.checkpoint_interval_ms = 0;
	cfg.compact_bytes          = 1; // force compaction on any checkpoint
	sm.SetConfig(cfg);

	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));

	// Ensure there is at least one non-checkpoint record on disk first.
	b.insert_text(0, 0, std::string("abc"));
	sm.Flush(&b);

	// Now emit a checkpoint; compaction should rewrite the file to just that checkpoint.
	sm.Checkpoint(&b);
	sm.Flush(&b);

	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	const std::string expected  = buffer_bytes_via_views(b);

	// Close journal.
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b);

	const std::string bytes               = read_file_bytes(swap_path);
	const std::vector<std::uint8_t> types = record_types_from_bytes(bytes);
	ASSERT_EQ(types.size(), (std::size_t) 1);
	ASSERT_EQ(types[0], (std::uint8_t) kte::SwapRecType::CHKPT);

	Buffer b2;
	ASSERT_TRUE(b2.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b2, swap_path, err));
	ASSERT_EQ(buffer_bytes_via_views(b2), expected);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


// Inserts larger than one record's 24-bit payload are split into several INS
// records; each must be positioned after the previous chunk, including when a
// chunk boundary falls mid-line.
TEST(SwapReplay_LargeInsert_SplitAcrossRecords)
{
	const std::string path = "./.kte_ut_swap_replay_large.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "tail\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	std::remove(kte::SwapManager::ComputeSwapPathForTests(b).c_str());

	// ~17 MiB of 1000-byte lines, so the 16 MiB chunk boundary splits a line.
	std::string big;
	const std::string line = std::string(999, 'a') + "\n";
	while (big.size() < (17u << 20))
		big += line;
	big += "mid";

	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));
	b.insert_text(0, 0, big);
	sm.Flush(&b);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	const std::string expected  = buffer_bytes_via_views(b);
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b, false);

	Buffer b2;
	ASSERT_TRUE(b2.OpenFromFile(path, err));
	std::string rerr;
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b2, swap_path, rerr));
	ASSERT_TRUE(buffer_bytes_via_views(b2) == expected);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


// When a record cannot be written, later position-based records no longer
// apply to the journal's state. The journal must drop them until a full
// checkpoint re-establishes the content, so replay still matches the buffer.
TEST(SwapReplay_LostRecord_ResyncsWithCheckpoint)
{
	const std::string path = "./.kte_ut_swap_replay_gap.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "base\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(b);
	std::remove(swap_path.c_str());

	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));

	b.insert_text(0, 0, std::string("A"));
	sm.Flush(&b);

	// Make the journal unopenable so the next record is lost.
	sm.ResetJournal(b); // closes and removes the file; base is now the disk file
	ASSERT_TRUE(b.SaveAs(path, err)); // disk = "Abase"
	std::filesystem::create_directory(swap_path);
	b.insert_text(0, 1, std::string("B"));
	sm.Flush(&b);
	std::filesystem::remove(swap_path);

	b.insert_text(0, 2, std::string("C")); // dropped; triggers a resync checkpoint
	sm.Flush(&b);
	b.insert_text(0, 3, std::string("D"));
	sm.Flush(&b);
	// If the writer handled B before its edit asked for a checkpoint, that
	// checkpoint failed too and the next attempt is rate-limited; the
	// per-frame retry then brings the journal back in step without an edit.
	std::this_thread::sleep_for(std::chrono::milliseconds(1100));
	sm.RetryGapCheckpoints();
	sm.Flush(&b);
	const std::string expected = buffer_bytes_via_views(b);
	ASSERT_EQ(expected, std::string("ABCDbase\n"));
	// Replay the journal as it stands mid-session (as after a crash); Detach
	// would append a final checkpoint that hides the problem.
	const std::string crash_copy = swap_path + ".crash";
	write_file_bytes(crash_copy, read_file_bytes(swap_path));
	b.SetSwapRecorder(nullptr);
	sm.Detach(&b, true);

	Buffer b2;
	ASSERT_TRUE(b2.OpenFromFile(path, err));
	std::string rerr;
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b2, crash_copy, rerr));
	ASSERT_EQ(buffer_bytes_via_views(b2), expected);

	std::remove(path.c_str());
	std::remove(crash_copy.c_str());
}
