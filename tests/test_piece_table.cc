#include "Test.h"
#include "PieceTable.h"
#include <string>

TEST(PieceTable_Insert_Delete_LineCount) {
    PieceTable pt;
    // start empty
    ASSERT_EQ(pt.Size(), (std::size_t)0);
    ASSERT_EQ(pt.LineCount(), (std::size_t)1); // empty buffer has 1 logical line

    // Insert some text with newlines
    const char *t = "abc\n123\nxyz"; // last line without trailing NL
    pt.Insert(0, t, 11);
    ASSERT_EQ(pt.Size(), (std::size_t)11);
    ASSERT_EQ(pt.LineCount(), (std::size_t)3);

    // Check get line
    ASSERT_EQ(pt.GetLine(0), std::string("abc"));
    ASSERT_EQ(pt.GetLine(1), std::string("123"));
    ASSERT_EQ(pt.GetLine(2), std::string("xyz"));

    // Delete middle line entirely including its trailing NL
    auto r = pt.GetLineRange(1); // [start,end) points to start of line 1 to start of line 2
    pt.Delete(r.first, r.second - r.first);
    ASSERT_EQ(pt.LineCount(), (std::size_t)2);
    ASSERT_EQ(pt.GetLine(0), std::string("abc"));
    ASSERT_EQ(pt.GetLine(1), std::string("xyz"));
}

TEST(PieceTable_LineCol_Conversions) {
    PieceTable pt;
    std::string s = "hello\nworld\n"; // two lines with trailing NL
    pt.Insert(0, s.data(), s.size());

    // Byte offsets of starts
    auto off0 = pt.LineColToByteOffset(0, 0);
    auto off1 = pt.LineColToByteOffset(1, 0);
    auto off2 = pt.LineColToByteOffset(2, 0); // EOF
    ASSERT_EQ(off0, (std::size_t)0);
    ASSERT_EQ(off1, (std::size_t)6); // "hello\n"
    ASSERT_EQ(off2, pt.Size());

    auto lc0 = pt.ByteOffsetToLineCol(0);
    auto lc1 = pt.ByteOffsetToLineCol(6);
    ASSERT_EQ(lc0.first, (std::size_t)0);
    ASSERT_EQ(lc0.second, (std::size_t)0);
    ASSERT_EQ(lc1.first, (std::size_t)1);
    ASSERT_EQ(lc1.second, (std::size_t)0);
}
