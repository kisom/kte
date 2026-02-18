#include "Test.h"

#include "Buffer.h"
#include "Swap.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>


// CRC32 helper (same algorithm as SwapManager::crc32)
static std::uint32_t
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


// Build a valid 64-byte swap file header
static std::string
build_swap_header()
{
	std::uint8_t hdr[64];
	std::memset(hdr, 0, sizeof(hdr));
	// Magic
	const std::uint8_t magic[8] = {'K', 'T', 'E', '_', 'S', 'W', 'P', '\0'};
	std::memcpy(hdr, magic, 8);
	// Version = 1 (little-endian)
	hdr[8]  = 1;
	hdr[9]  = 0;
	hdr[10] = 0;
	hdr[11] = 0;
	// Flags = 0
	// Created time (just use 0 for tests)
	return std::string(reinterpret_cast<char *>(hdr), sizeof(hdr));
}


// Build a swap record: [type u8][len u24][payload][crc32 u32]
static std::string
build_swap_record(std::uint8_t type, const std::vector<std::uint8_t> &payload)
{
	std::vector<std::uint8_t> record;

	// Record header: type(1) + length(3)
	record.push_back(type);
	std::uint32_t len = static_cast<std::uint32_t>(payload.size());
	record.push_back(static_cast<std::uint8_t>(len & 0xFFu));
	record.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFFu));
	record.push_back(static_cast<std::uint8_t>((len >> 16) & 0xFFu));

	// Payload
	record.insert(record.end(), payload.begin(), payload.end());

	// CRC32 (compute over header + payload)
	std::uint32_t crc = crc32(record.data(), record.size());
	record.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
	record.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFFu));
	record.push_back(static_cast<std::uint8_t>((crc >> 16) & 0xFFu));
	record.push_back(static_cast<std::uint8_t>((crc >> 24) & 0xFFu));

	return std::string(reinterpret_cast<char *>(record.data()), record.size());
}


// Build complete swap file with header and records
static std::string
build_swap_file(const std::vector<std::string> &records)
{
	std::string file = build_swap_header();
	for (const auto &rec: records) {
		file += rec;
	}
	return file;
}


// Write bytes to file
static void
write_file_bytes(const std::string &path, const std::string &bytes)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}


// Helper to encode u32 little-endian
static void
put_u32_le(std::vector<std::uint8_t> &out, std::uint32_t v)
{
	out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
	out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
	out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
	out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}


//=============================================================================
// 1. MINIMUM VALID PAYLOAD SIZE TESTS
//=============================================================================

TEST (SwapEdge_INS_MinimumValidPayload)
{
	const std::string path      = "./.kte_ut_edge_ins_min.txt";
	const std::string swap_path = "./.kte_ut_edge_ins_min.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// INS record: encver(1) + row(4) + col(4) + nbytes(4) = 13 bytes minimum
	// nbytes=0 means zero-length insertion
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row
	put_u32_le(payload, 0); // col
	put_u32_le(payload, 0); // nbytes=0

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b, swap_path, err));

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_DEL_MinimumValidPayload)
{
	const std::string path      = "./.kte_ut_edge_del_min.txt";
	const std::string swap_path = "./.kte_ut_edge_del_min.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// DEL record: encver(1) + row(4) + col(4) + dlen(4) = 13 bytes minimum
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row
	put_u32_le(payload, 0); // col
	put_u32_le(payload, 0); // dlen=0

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::DEL), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b, swap_path, err));

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_SPLIT_MinimumValidPayload)
{
	const std::string path      = "./.kte_ut_edge_split_min.txt";
	const std::string swap_path = "./.kte_ut_edge_split_min.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// SPLIT record: encver(1) + row(4) + col(4) = 9 bytes minimum
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row
	put_u32_le(payload, 0); // col

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::SPLIT), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b, swap_path, err));

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_JOIN_MinimumValidPayload)
{
	const std::string path      = "./.kte_ut_edge_join_min.txt";
	const std::string swap_path = "./.kte_ut_edge_join_min.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\nworld\n");

	// JOIN record: encver(1) + row(4) = 5 bytes minimum
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::JOIN), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b, swap_path, err));

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_CHKPT_MinimumValidPayload)
{
	const std::string path      = "./.kte_ut_edge_chkpt_min.txt";
	const std::string swap_path = "./.kte_ut_edge_chkpt_min.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// CHKPT record: encver(1) + nbytes(4) = 5 bytes minimum
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // nbytes=0

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::CHKPT), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b, swap_path, err));

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


//=============================================================================
// 2. TRUNCATED PAYLOAD TESTS (BELOW MINIMUM)
//=============================================================================

TEST (SwapEdge_INS_TruncatedPayload_1Byte)
{
	const std::string path      = "./.kte_ut_edge_ins_trunc1.txt";
	const std::string swap_path = "./.kte_ut_edge_ins_trunc1.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// INS record with only 1 byte (just encver)
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver only

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("INS payload too short") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_INS_TruncatedPayload_5Bytes)
{
	const std::string path      = "./.kte_ut_edge_ins_trunc5.txt";
	const std::string swap_path = "./.kte_ut_edge_ins_trunc5.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// INS record with 5 bytes (encver + row only)
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("INS payload too short") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_DEL_TruncatedPayload_9Bytes)
{
	const std::string path      = "./.kte_ut_edge_del_trunc9.txt";
	const std::string swap_path = "./.kte_ut_edge_del_trunc9.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// DEL record with 9 bytes (encver + row + col, missing dlen)
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row
	put_u32_le(payload, 0); // col
	// missing dlen

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::DEL), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("DEL payload too short") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_SPLIT_TruncatedPayload_5Bytes)
{
	const std::string path      = "./.kte_ut_edge_split_trunc5.txt";
	const std::string swap_path = "./.kte_ut_edge_split_trunc5.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// SPLIT record with 5 bytes (encver + row, missing col)
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row
	// missing col

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::SPLIT), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("SPLIT payload too short") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_JOIN_TruncatedPayload_1Byte)
{
	const std::string path      = "./.kte_ut_edge_join_trunc1.txt";
	const std::string swap_path = "./.kte_ut_edge_join_trunc1.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\nworld\n");

	// JOIN record with 1 byte (just encver)
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver only

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::JOIN), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("JOIN payload too short") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_CHKPT_TruncatedPayload_3Bytes)
{
	const std::string path      = "./.kte_ut_edge_chkpt_trunc3.txt";
	const std::string swap_path = "./.kte_ut_edge_chkpt_trunc3.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// CHKPT record with 3 bytes (encver + partial nbytes)
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	payload.push_back(0); // partial nbytes (only 2 bytes instead of 4)
	payload.push_back(0);

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::CHKPT), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("CHKPT payload too short") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


//=============================================================================
// 3. DATA OVERFLOW TESTS
//=============================================================================

TEST (SwapEdge_INS_TruncatedData_NbytesExceedsPayload)
{
	const std::string path      = "./.kte_ut_edge_ins_overflow.txt";
	const std::string swap_path = "./.kte_ut_edge_ins_overflow.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// INS record where nbytes=100 but payload only contains 13 bytes total
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row
	put_u32_le(payload, 0); // col
	put_u32_le(payload, 100); // nbytes=100 (but no data follows)

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("Truncated INS payload bytes") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_CHKPT_TruncatedData_NbytesExceedsPayload)
{
	const std::string path      = "./.kte_ut_edge_chkpt_overflow.txt";
	const std::string swap_path = "./.kte_ut_edge_chkpt_overflow.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// CHKPT record where nbytes=1000 but payload only contains 5 bytes total
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 1000); // nbytes=1000 (but no data follows)

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::CHKPT), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("Truncated CHKPT payload bytes") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


//=============================================================================
// 4. UNSUPPORTED ENCODING VERSION TESTS
//=============================================================================

TEST (SwapEdge_INS_UnsupportedEncodingVersion)
{
	const std::string path      = "./.kte_ut_edge_ins_badenc.txt";
	const std::string swap_path = "./.kte_ut_edge_ins_badenc.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// INS record with encver=2 (unsupported)
	std::vector<std::uint8_t> payload;
	payload.push_back(2); // encver=2 (unsupported)
	put_u32_le(payload, 0); // row
	put_u32_le(payload, 0); // col
	put_u32_le(payload, 0); // nbytes

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("Unsupported swap payload encoding") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_CHKPT_UnsupportedEncodingVersion)
{
	const std::string path      = "./.kte_ut_edge_chkpt_badenc.txt";
	const std::string swap_path = "./.kte_ut_edge_chkpt_badenc.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// CHKPT record with encver=99 (unsupported)
	std::vector<std::uint8_t> payload;
	payload.push_back(99); // encver=99 (unsupported)
	put_u32_le(payload, 0); // nbytes

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::CHKPT), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("Unsupported swap checkpoint encoding") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


//=============================================================================
// 5. BOUNDARY CONDITION TESTS
//=============================================================================

TEST (SwapEdge_INS_ExactlyEnoughBytes)
{
	const std::string path      = "./.kte_ut_edge_ins_exact.txt";
	const std::string swap_path = "./.kte_ut_edge_ins_exact.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// INS record with nbytes=10 and exactly 23 bytes total (13 header + 10 data)
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row
	put_u32_le(payload, 0); // col
	put_u32_le(payload, 10); // nbytes=10
	// Add exactly 10 bytes of data
	for (int i = 0; i < 10; i++) {
		payload.push_back('X');
	}

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b, swap_path, err));

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_INS_OneByteTooFew)
{
	const std::string path      = "./.kte_ut_edge_ins_toofew.txt";
	const std::string swap_path = "./.kte_ut_edge_ins_toofew.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// INS record with nbytes=10 but only 22 bytes total (13 header + 9 data)
	std::vector<std::uint8_t> payload;
	payload.push_back(1); // encver
	put_u32_le(payload, 0); // row
	put_u32_le(payload, 0); // col
	put_u32_le(payload, 10); // nbytes=10
	// Add only 9 bytes of data (one too few)
	for (int i = 0; i < 9; i++) {
		payload.push_back('X');
	}

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("Truncated INS payload bytes") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


//=============================================================================
// 6. MIXED VALID AND INVALID RECORDS
//=============================================================================

TEST (SwapEdge_MixedRecords_ValidThenInvalid)
{
	const std::string path      = "./.kte_ut_edge_mixed1.txt";
	const std::string swap_path = "./.kte_ut_edge_mixed1.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// First record: valid INS
	std::vector<std::uint8_t> payload1;
	payload1.push_back(1); // encver
	put_u32_le(payload1, 0); // row
	put_u32_le(payload1, 0); // col
	put_u32_le(payload1, 1); // nbytes=1
	payload1.push_back('X'); // data

	std::string rec1 = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload1);

	// Second record: truncated DEL
	std::vector<std::uint8_t> payload2;
	payload2.push_back(1); // encver only

	std::string rec2 = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::DEL), payload2);

	std::string file = build_swap_file({rec1, rec2});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("DEL payload too short") != std::string::npos);

	// Verify first INS was applied before failure
	auto view = b.GetLineView(0);
	std::string line(view.data(), view.size());
	ASSERT_TRUE(line.find('X') != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


TEST (SwapEdge_MixedRecords_MultipleValidOneInvalid)
{
	const std::string path      = "./.kte_ut_edge_mixed2.txt";
	const std::string swap_path = "./.kte_ut_edge_mixed2.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "ab\n");

	// First record: valid INS at (0,0)
	std::vector<std::uint8_t> payload1;
	payload1.push_back(1);
	put_u32_le(payload1, 0);
	put_u32_le(payload1, 0);
	put_u32_le(payload1, 1);
	payload1.push_back('X');
	std::string rec1 = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload1);

	// Second record: valid INS at (0,1)
	std::vector<std::uint8_t> payload2;
	payload2.push_back(1);
	put_u32_le(payload2, 0);
	put_u32_le(payload2, 1);
	put_u32_le(payload2, 1);
	payload2.push_back('Y');
	std::string rec2 = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload2);

	// Third record: truncated SPLIT
	std::vector<std::uint8_t> payload3;
	payload3.push_back(1); // encver only
	std::string rec3 = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::SPLIT), payload3);

	std::string file = build_swap_file({rec1, rec2, rec3});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("SPLIT payload too short") != std::string::npos);

	// Verify first two INS were applied
	auto view = b.GetLineView(0);
	std::string line(view.data(), view.size());
	ASSERT_TRUE(line.find('X') != std::string::npos);
	ASSERT_TRUE(line.find('Y') != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


//=============================================================================
// 7. EMPTY PAYLOAD TEST
//=============================================================================

TEST (SwapEdge_EmptyPayload_INS)
{
	const std::string path      = "./.kte_ut_edge_empty.txt";
	const std::string swap_path = "./.kte_ut_edge_empty.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// INS record with zero-length payload
	std::vector<std::uint8_t> payload; // empty

	std::string rec  = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);
	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("INS payload too short") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}


//=============================================================================
// 8. CRC MISMATCH TEST
//=============================================================================

TEST (SwapEdge_ValidStructure_BadCRC)
{
	const std::string path      = "./.kte_ut_edge_badcrc.txt";
	const std::string swap_path = "./.kte_ut_edge_badcrc.swp";
	std::remove(path.c_str());
	std::remove(swap_path.c_str());

	write_file_bytes(path, "hello\n");

	// Build a valid INS record
	std::vector<std::uint8_t> payload;
	payload.push_back(1);
	put_u32_le(payload, 0);
	put_u32_le(payload, 0);
	put_u32_le(payload, 1);
	payload.push_back('X');

	std::string rec = build_swap_record(static_cast<std::uint8_t>(kte::SwapRecType::INS), payload);

	// Corrupt the CRC (last 4 bytes)
	rec[rec.size() - 1] ^= 0xFF;

	std::string file = build_swap_file({rec});
	write_file_bytes(swap_path, file);

	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(path, err));
	ASSERT_EQ(kte::SwapManager::ReplayFile(b, swap_path, err), false);
	ASSERT_TRUE(err.find("CRC mismatch") != std::string::npos);

	std::remove(path.c_str());
	std::remove(swap_path.c_str());
}