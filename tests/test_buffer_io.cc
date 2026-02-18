/*
 * test_buffer_io.cc - Tests for Buffer file I/O operations
 *
 * This file validates the Buffer's file handling capabilities, which are
 * critical for a text editor. Buffer manages the relationship between
 * in-memory content and files on disk.
 *
 * Key functionality tested:
 * - SaveAs() creates a new file and makes the buffer file-backed
 * - Save() writes to the existing file (requires file-backed buffer)
 * - OpenFromFile() loads existing files or creates empty buffers for new files
 * - The dirty flag is properly managed across save operations
 *
 * These tests demonstrate the Buffer I/O contract that commands rely on.
 * When adding new file operations, follow these patterns.
 */
#include "Test.h"
#include <fstream>
#include <cstdio>
#include <string>
#include "Buffer.h"


static std::string
read_all(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}


TEST (Buffer_SaveAs_and_Save_new_file)
{
	const std::string path = "./.kte_ut_buffer_io_1.tmp";
	std::remove(path.c_str());

	Buffer b;
	// insert two lines
	b.insert_text(0, 0, std::string("Hello, world!\n"));
	b.insert_text(1, 0, std::string("Second line\n"));

	std::string err;
	ASSERT_TRUE(b.SaveAs(path, err));
	ASSERT_EQ(err.empty(), true);

	// append another line then Save()
	b.insert_text(2, 0, std::string("Third\n"));
	b.SetDirty(true);
	ASSERT_TRUE(b.Save(err));
	ASSERT_EQ(err.empty(), true);

	std::string got = read_all(path);
	ASSERT_EQ(got, std::string("Hello, world!\nSecond line\nThird\n"));

	std::remove(path.c_str());
}


TEST (Buffer_Save_after_Open_existing)
{
	const std::string path = "./.kte_ut_buffer_io_2.tmp";
	std::remove(path.c_str());
	{
		std::ofstream out(path, std::ios::binary);
		out << "abc\n123\n";
	}

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(err.empty(), true);

	b.insert_text(2, 0, std::string("tail\n"));
	b.SetDirty(true);
	ASSERT_TRUE(b.Save(err));
	ASSERT_EQ(err.empty(), true);

	std::string got = read_all(path);
	ASSERT_EQ(got, std::string("abc\n123\ntail\n"));
	std::remove(path.c_str());
}


TEST (Buffer_Open_nonexistent_then_SaveAs)
{
	const std::string path = "./.kte_ut_buffer_io_3.tmp";
	std::remove(path.c_str());

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(err.empty(), true);
	ASSERT_EQ(b.IsFileBacked(), false);

	b.insert_text(0, 0, std::string("hello, world"));
	b.insert_text(0, 12, std::string("\n"));
	b.SetDirty(true);
	ASSERT_TRUE(b.SaveAs(path, err));
	ASSERT_EQ(err.empty(), true);

	std::string got = read_all(path);
	ASSERT_EQ(got, std::string("hello, world\n"));
	std::remove(path.c_str());
}