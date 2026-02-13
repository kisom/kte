#include "Test.h"

#include "Buffer.h"
#include "Swap.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
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


TEST (SwapReplay_RecordFlushReopenReplay_ExactBytesMatch)
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


TEST (SwapReplay_TruncatedLog_FailsSafely)
{
	const std::string path = "./.kte_ut_swap_replay_2.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "hello\n");

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));

	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));
	b.insert_text(0, 0, std::string("X"));
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
	ASSERT_EQ(kte::SwapManager::ReplayFile(b2, trunc_path, rerr), false);
	ASSERT_EQ(rerr.empty(), false);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
	std::remove(trunc_path.c_str());
}


TEST (SwapReplay_Checkpoint_Midstream_ExactBytesMatch)
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


TEST (SwapCompaction_RewritesToSingleCheckpoint)
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