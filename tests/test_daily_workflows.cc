#include "Test.h"

#include "Command.h"
#include "Editor.h"

#include "tests/TestHarness.h" // for ktet::InstallDefaultCommandsOnce

#include <cstdio>
#include <filesystem>
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


TEST(DailyWorkflow_OpenEditSave_Transcript)
{
	ktet::InstallDefaultCommandsOnce();

	const std::string path = "./.kte_ut_daily_open_edit_save.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "one\n");
	const std::string npath = std::filesystem::canonical(path).string();

	Editor ed;
	ed.SetDimensions(24, 80);
	// Seed an empty buffer so OpenFile can reuse it.
	{
		Buffer scratch;
		ed.AddBuffer(std::move(scratch));
	}

	std::string err;
	ASSERT_TRUE(ed.OpenFile(path, err));
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_EQ(ed.CurrentBuffer()->Filename(), npath);

	// Append two new lines via commands (no UI).
	ASSERT_TRUE(Execute(ed, CommandId::MoveFileEnd));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "two"));
	ASSERT_TRUE(Execute(ed, CommandId::Newline));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "three"));

	ASSERT_TRUE(Execute(ed, CommandId::Save));
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_EQ(read_file_bytes(npath), buffer_bytes_via_views(*ed.CurrentBuffer()));

	std::remove(path.c_str());
	std::remove(npath.c_str());
}


TEST(DailyWorkflow_MultiBufferSwitchClose_Transcript)
{
	ktet::InstallDefaultCommandsOnce();

	const std::string p1 = "./.kte_ut_daily_buf_1.txt";
	const std::string p2 = "./.kte_ut_daily_buf_2.txt";
	std::remove(p1.c_str());
	std::remove(p2.c_str());
	write_file_bytes(p1, "aaa\n");
	write_file_bytes(p2, "bbb\n");
	const std::string np1 = std::filesystem::canonical(p1).string();
	const std::string np2 = std::filesystem::canonical(p2).string();

	Editor ed;
	ed.SetDimensions(24, 80);
	{
		Buffer scratch;
		ed.AddBuffer(std::move(scratch));
	}

	std::string err;
	ASSERT_TRUE(ed.OpenFile(p1, err));
	ASSERT_TRUE(ed.OpenFile(p2, err));
	ASSERT_EQ(ed.BufferCount(), (std::size_t) 2);
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_EQ(ed.CurrentBuffer()->Filename(), np2);

	// Switch back and forth.
	ASSERT_TRUE(Execute(ed, CommandId::BufferPrev));
	ASSERT_EQ(ed.CurrentBuffer()->Filename(), np1);
	ASSERT_TRUE(Execute(ed, CommandId::BufferNext));
	ASSERT_EQ(ed.CurrentBuffer()->Filename(), np2);

	// Close current buffer (p2); ensure we land on p1.
	ASSERT_TRUE(Execute(ed, CommandId::BufferClose));
	ASSERT_EQ(ed.BufferCount(), (std::size_t) 1);
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_EQ(ed.CurrentBuffer()->Filename(), np1);

	std::remove(p1.c_str());
	std::remove(p2.c_str());
	std::remove(np1.c_str());
	std::remove(np2.c_str());
}


TEST(DailyWorkflow_CrashRecovery_SwapReplay_Transcript)
{
	ktet::InstallDefaultCommandsOnce();

	const std::string path = "./.kte_ut_daily_swap_recover.txt";
	std::remove(path.c_str());
	write_file_bytes(path, "base\nline2\n");

	Editor ed;
	ed.SetDimensions(24, 80);
	{
		Buffer scratch;
		ed.AddBuffer(std::move(scratch));
	}

	std::string err;
	ASSERT_TRUE(ed.OpenFile(path, err));
	Buffer *buf = ed.CurrentBuffer();
	ASSERT_TRUE(buf != nullptr);

	// Make unsaved edits through command execution.
	ASSERT_TRUE(Execute(ed, CommandId::MoveFileStart));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "X"));
	ASSERT_TRUE(Execute(ed, CommandId::MoveDown));
	ASSERT_TRUE(Execute(ed, CommandId::MoveHome));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "ZZ"));
	ASSERT_TRUE(Execute(ed, CommandId::MoveFileEnd));
	ASSERT_TRUE(Execute(ed, CommandId::InsertText, "TAIL"));

	// Ensure journal is durable and capture expected bytes.
	ed.Swap()->Flush(buf);
	const std::string swap_path = kte::SwapManager::ComputeSwapPathForTests(*buf);
	const std::string expected  = buffer_bytes_via_views(*buf);

	// "Crash": reopen from disk (original file content) into a fresh Buffer and replay.
	Buffer recovered;
	ASSERT_TRUE(recovered.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(recovered, swap_path, err));
	ASSERT_EQ(buffer_bytes_via_views(recovered), expected);

	// Cleanup.
	ed.Swap()->Detach(buf);
	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}
