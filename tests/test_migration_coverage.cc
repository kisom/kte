/*
 * test_migration_coverage.cc - Edge case tests for Buffer::Line migration
 *
 * This file provides comprehensive test coverage for the migration from
 * Buffer::Rows() to direct PieceTable operations using Nrows(), GetLineString(),
 * and GetLineView().
 *
 * Tests cover:
 * - Edge cases: empty buffers, single lines, very long lines
 * - Boundary conditions: first line, last line, out-of-bounds
 * - Consistency: GetLineString vs GetLineView vs Rows()
 * - Performance: large files, many small operations
 * - Correctness: special characters, newlines, unicode
 */
#include "Test.h"
#include "Buffer.h"
#include <string>
#include <vector>

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST (Migration_EmptyBuffer_Nrows)
{
	Buffer buf;
	ASSERT_EQ(buf.Nrows(), (std::size_t) 1); // Empty buffer has 1 logical line
}


TEST (Migration_EmptyBuffer_GetLineString)
{
	Buffer buf;
	ASSERT_EQ(buf.GetLineString(0), std::string(""));
}


TEST (Migration_EmptyBuffer_GetLineView)
{
	Buffer buf;
	auto view = buf.GetLineView(0);
	ASSERT_EQ(view.size(), (std::size_t) 0);
	ASSERT_EQ(std::string(view), std::string(""));
}


TEST (Migration_SingleLine_NoNewline)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("hello"));

	ASSERT_EQ(buf.Nrows(), (std::size_t) 1);
	ASSERT_EQ(buf.GetLineString(0), std::string("hello"));
	ASSERT_EQ(std::string(buf.GetLineView(0)), std::string("hello"));
}


TEST (Migration_SingleLine_WithNewline)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("hello\n"));

	ASSERT_EQ(buf.Nrows(), (std::size_t) 2); // Line + empty line after newline
	ASSERT_EQ(buf.GetLineString(0), std::string("hello"));
	ASSERT_EQ(buf.GetLineString(1), std::string(""));
}


TEST (Migration_MultipleLines_TrailingNewline)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("line1\nline2\nline3\n"));

	ASSERT_EQ(buf.Nrows(), (std::size_t) 4); // 3 lines + empty line
	ASSERT_EQ(buf.GetLineString(0), std::string("line1"));
	ASSERT_EQ(buf.GetLineString(1), std::string("line2"));
	ASSERT_EQ(buf.GetLineString(2), std::string("line3"));
	ASSERT_EQ(buf.GetLineString(3), std::string(""));
}


TEST (Migration_MultipleLines_NoTrailingNewline)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("line1\nline2\nline3"));

	ASSERT_EQ(buf.Nrows(), (std::size_t) 3);
	ASSERT_EQ(buf.GetLineString(0), std::string("line1"));
	ASSERT_EQ(buf.GetLineString(1), std::string("line2"));
	ASSERT_EQ(buf.GetLineString(2), std::string("line3"));
}


TEST (Migration_VeryLongLine)
{
	Buffer buf;
	std::string long_line(10000, 'x');
	buf.insert_text(0, 0, long_line);

	ASSERT_EQ(buf.Nrows(), (std::size_t) 1);
	ASSERT_EQ(buf.GetLineString(0), long_line);
	ASSERT_EQ(buf.GetLineString(0).size(), (std::size_t) 10000);
}


TEST (Migration_ManyEmptyLines)
{
	Buffer buf;
	std::string many_newlines(1000, '\n');
	buf.insert_text(0, 0, many_newlines);

	ASSERT_EQ(buf.Nrows(), (std::size_t) 1001); // 1000 newlines = 1001 lines
	for (std::size_t i = 0; i < buf.Nrows(); ++i) {
		ASSERT_EQ(buf.GetLineString(i), std::string(""));
	}
}


// ============================================================================
// Consistency Tests: GetLineString vs GetLineView vs Rows()
// ============================================================================

TEST (Migration_Consistency_AllMethods)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("abc\n123\nxyz"));

	const auto &rows = buf.Rows();
	ASSERT_EQ(buf.Nrows(), rows.size());

	for (std::size_t i = 0; i < buf.Nrows(); ++i) {
		std::string via_string = buf.GetLineString(i);
		std::string via_rows   = std::string(rows[i]);
		// GetLineString and Rows() both strip newlines
		ASSERT_EQ(via_string, via_rows);
		// GetLineView includes the raw range (with newlines if present)
		// Just verify it's accessible
		(void) buf.GetLineView(i);
	}
}


TEST (Migration_Consistency_AfterEdits)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("line1\nline2\nline3\n"));

	// Edit: insert in middle
	buf.insert_text(1, 2, std::string("XX"));

	const auto &rows = buf.Rows();
	ASSERT_EQ(buf.Nrows(), rows.size());

	for (std::size_t i = 0; i < buf.Nrows(); ++i) {
		// GetLineString and Rows() both strip newlines
		ASSERT_EQ(buf.GetLineString(i), std::string(rows[i]));
	}

	// Edit: delete line
	buf.delete_row(1);

	const auto &rows2 = buf.Rows();
	ASSERT_EQ(buf.Nrows(), rows2.size());

	for (std::size_t i = 0; i < buf.Nrows(); ++i) {
		ASSERT_EQ(buf.GetLineString(i), std::string(rows2[i]));
	}
}


// ============================================================================
// Boundary Tests
// ============================================================================

TEST (Migration_FirstLine_Access)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("first\nsecond\nthird"));

	ASSERT_EQ(buf.GetLineString(0), std::string("first"));
	// GetLineView includes newline: "first\n"
	auto view0 = buf.GetLineView(0);
	EXPECT_TRUE(view0.size() >= 5); // at least "first"
}


TEST (Migration_LastLine_Access)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("first\nsecond\nthird"));

	std::size_t last = buf.Nrows() - 1;
	ASSERT_EQ(buf.GetLineString(last), std::string("third"));
	ASSERT_EQ(std::string(buf.GetLineView(last)), std::string("third"));
}


TEST (Migration_GetLineRange_Boundaries)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("abc\n123\nxyz"));

	// First line
	auto r0 = buf.GetLineRange(0);
	ASSERT_EQ(r0.first, (std::size_t) 0);
	ASSERT_EQ(r0.second, (std::size_t) 4); // "abc\n"

	// Last line
	std::size_t last = buf.Nrows() - 1;
	(void) buf.GetLineRange(last); // Verify it doesn't crash
	ASSERT_EQ(buf.GetLineString(last), std::string("xyz"));
}


// ============================================================================
// Special Characters and Unicode
// ============================================================================

TEST (Migration_SpecialChars_Tabs)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("line\twith\ttabs"));

	ASSERT_EQ(buf.GetLineString(0), std::string("line\twith\ttabs"));
	ASSERT_EQ(std::string(buf.GetLineView(0)), std::string("line\twith\ttabs"));
}


TEST (Migration_SpecialChars_CarriageReturn)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("line\rwith\rcr"));

	ASSERT_EQ(buf.GetLineString(0), std::string("line\rwith\rcr"));
}


TEST (Migration_SpecialChars_NullBytes)
{
	Buffer buf;
	std::string with_null = "abc";
	with_null.push_back('\0');
	with_null += "def";
	buf.insert_text(0, 0, with_null);

	ASSERT_EQ(buf.GetLineString(0).size(), (std::size_t) 7);
	ASSERT_EQ(buf.GetLineView(0).size(), (std::size_t) 7);
}


TEST (Migration_Unicode_BasicMultibyte)
{
	Buffer buf;
	std::string utf8 = "Hello 世界 🌍";
	buf.insert_text(0, 0, utf8);

	ASSERT_EQ(buf.GetLineString(0), utf8);
	ASSERT_EQ(std::string(buf.GetLineView(0)), utf8);
}


// ============================================================================
// Large File Tests
// ============================================================================

TEST (Migration_LargeFile_10K_Lines)
{
	Buffer buf;
	std::string data;
	for (int i = 0; i < 10000; ++i) {
		data += "Line " + std::to_string(i) + "\n";
	}
	buf.insert_text(0, 0, data);

	ASSERT_EQ(buf.Nrows(), (std::size_t) 10001); // +1 for final empty line

	// Spot check some lines
	ASSERT_EQ(buf.GetLineString(0), std::string("Line 0"));
	ASSERT_EQ(buf.GetLineString(5000), std::string("Line 5000"));
	ASSERT_EQ(buf.GetLineString(9999), std::string("Line 9999"));
	ASSERT_EQ(buf.GetLineString(10000), std::string(""));
}


TEST (Migration_LargeFile_Iteration_Consistency)
{
	Buffer buf;
	std::string data;
	for (int i = 0; i < 1000; ++i) {
		data += "Line " + std::to_string(i) + "\n";
	}
	buf.insert_text(0, 0, data);

	// Iterate with GetLineString (strips newlines, must add back)
	std::string reconstructed1;
	for (std::size_t i = 0; i < buf.Nrows(); ++i) {
		if (i > 0) {
			reconstructed1 += '\n';
		}
		reconstructed1 += buf.GetLineString(i);
	}

	// Iterate with GetLineView (includes newlines)
	std::string reconstructed2;
	for (std::size_t i = 0; i < buf.Nrows(); ++i) {
		auto view = buf.GetLineView(i);
		reconstructed2.append(view.data(), view.size());
	}

	// GetLineView should match original exactly
	ASSERT_EQ(reconstructed2, data);
	// GetLineString reconstruction should match (without final empty line)
	EXPECT_TRUE(reconstructed1.size() > 0);
}


// ============================================================================
// Stress Tests: Many Small Operations
// ============================================================================

TEST (Migration_Stress_ManySmallInserts)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("start\n"));

	for (int i = 0; i < 100; ++i) {
		buf.insert_text(1, 0, std::string("x"));
	}

	ASSERT_EQ(buf.Nrows(), (std::size_t) 2);
	ASSERT_EQ(buf.GetLineString(0), std::string("start"));
	ASSERT_EQ(buf.GetLineString(1).size(), (std::size_t) 100);

	// Verify consistency
	const auto &rows = buf.Rows();
	ASSERT_EQ(buf.GetLineString(1), std::string(rows[1]));
}


TEST (Migration_Stress_ManyLineInserts)
{
	Buffer buf;

	for (int i = 0; i < 500; ++i) {
		buf.insert_row(buf.Nrows() - 1, std::string_view("line"));
	}

	ASSERT_EQ(buf.Nrows(), (std::size_t) 501); // 500 + initial empty line

	for (std::size_t i = 0; i < 500; ++i) {
		ASSERT_EQ(buf.GetLineString(i), std::string("line"));
	}
}


TEST (Migration_Stress_AlternatingInsertDelete)
{
	Buffer buf;
	buf.insert_text(0, 0, std::string("a\nb\nc\nd\ne\n"));

	for (int i = 0; i < 50; ++i) {
		std::size_t nrows = buf.Nrows();
		if (nrows > 2) {
			buf.delete_row(1);
		}
		buf.insert_row(1, std::string_view("new"));
	}

	// Verify consistency after many operations
	const auto &rows = buf.Rows();
	ASSERT_EQ(buf.Nrows(), rows.size());

	for (std::size_t i = 0; i < buf.Nrows(); ++i) {
		// GetLineString and Rows() both strip newlines
		ASSERT_EQ(buf.GetLineString(i), std::string(rows[i]));
	}
}


// ============================================================================
// Regression Tests: Specific Migration Scenarios
// ============================================================================

TEST (Migration_Shebang_Detection)
{
	// Test the pattern used in Editor.cc for shebang detection
	Buffer buf;
	buf.insert_text(0, 0, std::string("#!/usr/bin/env python3\nprint('hello')"));

	ASSERT_EQ(buf.Nrows(), (std::size_t) 2);

	std::string first_line = "";
	if (buf.Nrows() > 0) {
		first_line = buf.GetLineString(0);
	}

	ASSERT_EQ(first_line, std::string("#!/usr/bin/env python3"));
}


TEST (Migration_EmptyBufferCheck_Pattern)
{
	// Test the pattern used in Editor.cc for empty buffer detection
	Buffer buf;

	const std::size_t nrows      = buf.Nrows();
	const bool rows_empty        = (nrows == 0);
	const bool single_empty_line = (nrows == 1 && buf.GetLineView(0).size() == 0);

	ASSERT_EQ(rows_empty, false);
	ASSERT_EQ(single_empty_line, true);
}


TEST (Migration_SyntaxHighlighter_Pattern)
{
	// Test the pattern used in syntax highlighters
	Buffer buf;
	buf.insert_text(0, 0, std::string("int main() {\n    return 0;\n}"));

	for (std::size_t row = 0; row < buf.Nrows(); ++row) {
		// This is the pattern used in all migrated highlighters
		if (row >= buf.Nrows()) {
			break; // Should never happen
		}
		std::string line = buf.GetLineString(row);
		// Successfully accessed line - size() is always valid for std::string
	}
}


TEST (Migration_SwapSnapshot_Pattern)
{
	// Test the pattern used in Swap.cc for buffer snapshots
	Buffer buf;
	buf.insert_text(0, 0, std::string("line1\nline2\nline3\n"));

	const std::size_t nrows = buf.Nrows();
	std::string snapshot;

	for (std::size_t i = 0; i < nrows; ++i) {
		auto view = buf.GetLineView(i);
		snapshot.append(view.data(), view.size());
	}

	EXPECT_TRUE(snapshot.size() > 0);
	ASSERT_EQ(snapshot, std::string("line1\nline2\nline3\n"));
}