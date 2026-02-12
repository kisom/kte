#include "Test.h"

#include "Buffer.h"
#include "Swap.h"

#include <cstdio>
#include <fstream>
#include <string>


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