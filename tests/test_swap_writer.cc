#include "Test.h"

#include "Buffer.h"
#include "Swap.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <cstdio>
#include <unistd.h>

#include <sys/stat.h>

namespace {
std::vector<std::uint8_t>
read_all_bytes(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}


std::uint32_t
read_le32(const std::uint8_t *p)
{
	return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) |
	       ((std::uint32_t) p[3] << 24);
}


std::uint64_t
read_le64(const std::uint8_t *p)
{
	std::uint64_t v = 0;
	for (int i = 7; i >= 0; --i) {
		v = (v << 8) | p[i];
	}
	return v;
}


std::uint32_t
crc32(const std::uint8_t *data, std::size_t len, std::uint32_t seed = 0)
{
	static std::uint32_t table[256];
	static bool inited = false;
	if (!inited) {
		for (std::uint32_t i = 0; i < 256; ++i) {
			std::uint32_t c = i;
			for (int j = 0; j < 8; ++j)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		inited = true;
	}
	std::uint32_t c = ~seed;
	for (std::size_t i = 0; i < len; ++i)
		c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
	return ~c;
}
} // namespace


TEST(SwapWriter_Header_Records_And_CRC)
{
	const std::filesystem::path xdg_root = std::filesystem::temp_directory_path() /
	                                       (std::string("kte_ut_xdg_state_") + std::to_string((int) ::getpid()));
	std::filesystem::remove_all(xdg_root);

	const char *old_xdg_p        = std::getenv("XDG_STATE_HOME");
	const std::string old_xdg    = old_xdg_p ? std::string(old_xdg_p) : std::string();
	const std::string xdg_root_s = xdg_root.string();
	setenv("XDG_STATE_HOME", xdg_root_s.c_str(), 1);

	const std::string path = (xdg_root / "work" / "kte_ut_swap_writer.txt").string();
	std::filesystem::create_directories((xdg_root / "work"));

	// Clean up from prior runs
	std::remove(path.c_str());

	// Ensure file exists so buffer is file-backed
	{
		std::ofstream out(path, std::ios::binary);
		out << "";
	}

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_TRUE(err.empty());
	ASSERT_TRUE(b.IsFileBacked());

	kte::SwapManager sm;
	sm.Attach(&b);
	b.SetSwapRecorder(sm.RecorderFor(&b));
	const std::string swp = kte::SwapManager::ComputeSwapPathForTests(b);
	std::remove(swp.c_str());

	// Emit one INS and one DEL
	b.insert_text(0, 0, std::string_view("abc"));
	b.delete_text(0, 1, 1);

	// Ensure all records are written before reading
	sm.Flush(&b);
	sm.Detach(&b);
	b.SetSwapRecorder(nullptr);

	ASSERT_TRUE(std::filesystem::exists(swp));

	// Verify permissions 0600
	struct stat st{};
	ASSERT_TRUE(::stat(swp.c_str(), &st) == 0);
	ASSERT_EQ((st.st_mode & 0777), 0600);

	const std::vector<std::uint8_t> bytes = read_all_bytes(swp);
	ASSERT_TRUE(bytes.size() >= 64);

	// Header
	static const std::uint8_t magic[8] = {'K', 'T', 'E', '_', 'S', 'W', 'P', '\0'};
	for (int i = 0; i < 8; ++i)
		ASSERT_EQ(bytes[(std::size_t) i], magic[i]);
	ASSERT_EQ(read_le32(bytes.data() + 8), (std::uint32_t) 1);
	// flags currently 0
	ASSERT_EQ(read_le32(bytes.data() + 12), (std::uint32_t) 0);
	ASSERT_TRUE(read_le64(bytes.data() + 16) != 0);

	// Records
	std::vector<std::uint8_t> types;
	std::size_t off = 64;
	while (off < bytes.size()) {
		ASSERT_TRUE(bytes.size() - off >= 8); // at least header+crc
		const std::uint8_t type = bytes[off + 0];
		const std::uint32_t len = (std::uint32_t) bytes[off + 1] | ((std::uint32_t) bytes[off + 2] << 8) |
		                          ((std::uint32_t) bytes[off + 3] << 16);
		const std::size_t payload_off = off + 4;
		const std::size_t crc_off     = payload_off + len;
		ASSERT_TRUE(crc_off + 4 <= bytes.size());

		const std::uint32_t got_crc = read_le32(bytes.data() + crc_off);
		std::uint32_t c             = 0;
		c                           = crc32(bytes.data() + off, 4, c);
		c                           = crc32(bytes.data() + payload_off, len, c);
		ASSERT_EQ(got_crc, c);

		types.push_back(type);
		off = crc_off + 4;
	}

	ASSERT_EQ(types.size(), (std::size_t) 3);
	ASSERT_EQ(types[0], (std::uint8_t) kte::SwapRecType::INS);
	ASSERT_EQ(types[1], (std::uint8_t) kte::SwapRecType::DEL);
	ASSERT_EQ(types[2], (std::uint8_t) kte::SwapRecType::CHKPT);

	std::remove(path.c_str());
	std::remove(swp.c_str());
	if (!old_xdg.empty()) {
		setenv("XDG_STATE_HOME", old_xdg.c_str(), 1);
	} else {
		unsetenv("XDG_STATE_HOME");
	}
	std::filesystem::remove_all(xdg_root);
}


TEST(SwapWriter_NoStomp_SameBasename)
{
	const std::filesystem::path xdg_root = std::filesystem::temp_directory_path() /
	                                       (std::string("kte_ut_xdg_state_nostomp_") + std::to_string(
		                                        (int) ::getpid()));
	std::filesystem::remove_all(xdg_root);
	std::filesystem::create_directories(xdg_root);

	const char *old_xdg_p        = std::getenv("XDG_STATE_HOME");
	const std::string old_xdg    = old_xdg_p ? std::string(old_xdg_p) : std::string();
	const std::string xdg_root_s = xdg_root.string();
	setenv("XDG_STATE_HOME", xdg_root_s.c_str(), 1);

	const std::filesystem::path d1 = xdg_root / "p1";
	const std::filesystem::path d2 = xdg_root / "p2";
	std::filesystem::create_directories(d1);
	std::filesystem::create_directories(d2);
	const std::filesystem::path f1 = d1 / "same.txt";
	const std::filesystem::path f2 = d2 / "same.txt";

	{
		std::ofstream out(f1.string(), std::ios::binary);
		out << "";
	}
	{
		std::ofstream out(f2.string(), std::ios::binary);
		out << "";
	}

	Buffer b1;
	Buffer b2;
	std::string err;
	ASSERT_TRUE(b1.OpenFromFile(f1.string(), err));
	ASSERT_TRUE(err.empty());
	ASSERT_TRUE(b2.OpenFromFile(f2.string(), err));
	ASSERT_TRUE(err.empty());

	const std::string swp1 = kte::SwapManager::ComputeSwapPathForTests(b1);
	const std::string swp2 = kte::SwapManager::ComputeSwapPathForTests(b2);
	ASSERT_TRUE(swp1 != swp2);

	// Actually write to both to ensure one doesn't clobber the other.
	kte::SwapManager sm;
	sm.Attach(&b1);
	sm.Attach(&b2);
	b1.SetSwapRecorder(sm.RecorderFor(&b1));
	b2.SetSwapRecorder(sm.RecorderFor(&b2));

	b1.insert_text(0, 0, std::string_view("one"));
	b2.insert_text(0, 0, std::string_view("two"));
	sm.Flush();

	ASSERT_TRUE(std::filesystem::exists(swp1));
	ASSERT_TRUE(std::filesystem::exists(swp2));
	ASSERT_TRUE(std::filesystem::file_size(swp1) >= 64);
	ASSERT_TRUE(std::filesystem::file_size(swp2) >= 64);

	sm.Detach(&b1);
	sm.Detach(&b2);
	b1.SetSwapRecorder(nullptr);
	b2.SetSwapRecorder(nullptr);

	std::remove(swp1.c_str());
	std::remove(swp2.c_str());
	std::remove(f1.string().c_str());
	std::remove(f2.string().c_str());
	if (!old_xdg.empty()) {
		setenv("XDG_STATE_HOME", old_xdg.c_str(), 1);
	} else {
		unsetenv("XDG_STATE_HOME");
	}
	std::filesystem::remove_all(xdg_root);
}
