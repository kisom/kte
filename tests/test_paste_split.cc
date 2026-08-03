// Tests for SplitPasteIntoCommands: pasted text must turn embedded line
// breaks into Newline commands so InsertText never receives a '\r' or '\n'.
#include "Test.h"

#include "PasteSplit.h"


namespace {

// Assert that no InsertText command carries an embedded newline/carriage
// return, mirroring the rejection in Command.cc's InsertText handler.
void
assert_no_newlines_in_inserts(const std::vector<MappedInput> &cmds)
{
	for (const auto &c : cmds) {
		if (c.id == CommandId::InsertText) {
			ASSERT_TRUE(c.arg.find('\n') == std::string::npos);
			ASSERT_TRUE(c.arg.find('\r') == std::string::npos);
		}
	}
}

} // namespace


TEST(PasteSplit_PlainTextNoNewline)
{
	auto cmds = SplitPasteIntoCommands("hello world");
	ASSERT_EQ(cmds.size(), 1u);
	ASSERT_TRUE(cmds[0].id == CommandId::InsertText);
	ASSERT_TRUE(cmds[0].arg == "hello world");
}


TEST(PasteSplit_UnixNewlines)
{
	auto cmds = SplitPasteIntoCommands("a\nb");
	assert_no_newlines_in_inserts(cmds);
	ASSERT_EQ(cmds.size(), 3u);
	ASSERT_TRUE(cmds[0].id == CommandId::InsertText && cmds[0].arg == "a");
	ASSERT_TRUE(cmds[1].id == CommandId::Newline);
	ASSERT_TRUE(cmds[2].id == CommandId::InsertText && cmds[2].arg == "b");
}


// Regression: CRLF clipboards left a '\r' in the InsertText segment, which
// InsertText rejected with "InsertText arg must not contain newlines".
TEST(PasteSplit_WindowsCRLF)
{
	auto cmds = SplitPasteIntoCommands("a\r\nb");
	assert_no_newlines_in_inserts(cmds);
	ASSERT_EQ(cmds.size(), 3u);
	ASSERT_TRUE(cmds[0].id == CommandId::InsertText && cmds[0].arg == "a");
	ASSERT_TRUE(cmds[1].id == CommandId::Newline);
	ASSERT_TRUE(cmds[2].id == CommandId::InsertText && cmds[2].arg == "b");
}


// Regression: a bare '\r' (classic Mac / some macOS apps) must also break lines.
TEST(PasteSplit_BareCR)
{
	auto cmds = SplitPasteIntoCommands("a\rb");
	assert_no_newlines_in_inserts(cmds);
	ASSERT_EQ(cmds.size(), 3u);
	ASSERT_TRUE(cmds[0].id == CommandId::InsertText && cmds[0].arg == "a");
	ASSERT_TRUE(cmds[1].id == CommandId::Newline);
	ASSERT_TRUE(cmds[2].id == CommandId::InsertText && cmds[2].arg == "b");
}


TEST(PasteSplit_BlankLinesPreserved)
{
	// Two line breaks between "a" and "b" => one empty line.
	auto cmds = SplitPasteIntoCommands("a\n\nb");
	assert_no_newlines_in_inserts(cmds);
	ASSERT_EQ(cmds.size(), 4u);
	ASSERT_TRUE(cmds[0].id == CommandId::InsertText && cmds[0].arg == "a");
	ASSERT_TRUE(cmds[1].id == CommandId::Newline);
	ASSERT_TRUE(cmds[2].id == CommandId::Newline);
	ASSERT_TRUE(cmds[3].id == CommandId::InsertText && cmds[3].arg == "b");
}


TEST(PasteSplit_TrailingNewline)
{
	auto cmds = SplitPasteIntoCommands("abc\n");
	assert_no_newlines_in_inserts(cmds);
	ASSERT_EQ(cmds.size(), 2u);
	ASSERT_TRUE(cmds[0].id == CommandId::InsertText && cmds[0].arg == "abc");
	ASSERT_TRUE(cmds[1].id == CommandId::Newline);
}


TEST(PasteSplit_MultilineCRLF)
{
	auto cmds = SplitPasteIntoCommands("line1\r\nline2\r\nline3");
	assert_no_newlines_in_inserts(cmds);
	ASSERT_EQ(cmds.size(), 5u);
	ASSERT_TRUE(cmds[0].arg == "line1");
	ASSERT_TRUE(cmds[1].id == CommandId::Newline);
	ASSERT_TRUE(cmds[2].arg == "line2");
	ASSERT_TRUE(cmds[3].id == CommandId::Newline);
	ASSERT_TRUE(cmds[4].arg == "line3");
}
