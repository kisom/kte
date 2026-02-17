#include "Test.h"

#include "Buffer.h"
#include "Command.h"
#include "Editor.h"

#include <iostream>
#include <string>


static std::string
to_string_rows(const Buffer &buf)
{
	std::string out;
	for (const auto &r: buf.Rows()) {
		out += static_cast<std::string>(r);
		out.push_back('\n');
	}
	return out;
}


TEST(ReflowParagraph_NumberedList_HangingIndent)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	// Two list items in one paragraph (no blank lines).
	// Second line of each item already uses a hanging indent.
	const std::string initial =
		"1. one two three four five six seven eight nine ten eleven\n"
		"   twelve thirteen fourteen\n"
		"10. alpha beta gamma delta epsilon zeta eta theta iota kappa lambda\n"
		"    mu nu xi omicron\n";
	b.insert_text(0, 0, initial);
	// Put cursor on first item
	b.SetCursor(0, 0);
	ed.AddBuffer(std::move(b));

	Buffer *buf = ed.CurrentBuffer();
	ASSERT_TRUE(buf != nullptr);

	const int width = 25;
	ASSERT_TRUE(Execute(ed, std::string("reflow-paragraph"), std::string(), width));

	const auto &rows = buf->Rows();
	ASSERT_TRUE(!rows.empty());
	const std::string dump = to_string_rows(*buf);

	// Find the start of the second item.
	bool any_too_long  = false;
	std::size_t idx_10 = rows.size();
	for (std::size_t i = 0; i < rows.size(); ++i) {
		const std::string line = static_cast<std::string>(rows[i]);
		if (static_cast<int>(line.size()) > width)
			any_too_long = true;
		if (line.rfind("10. ", 0) == 0) {
			idx_10 = i;
			break;
		}
	}
	ASSERT_TRUE(idx_10 < rows.size());
	if (any_too_long) {
		std::cerr << "Reflow produced a line longer than width=" << width << "\n";
		std::cerr << to_string_rows(*buf) << "\n";
	}
	EXPECT_TRUE(!any_too_long);

	// Item 1: first line has "1. ", continuation lines have 3 spaces.
	for (std::size_t i = 0; i < idx_10; ++i) {
		const std::string line = static_cast<std::string>(rows[i]);
		if (i == 0) {
			ASSERT_TRUE(line.rfind("1. ", 0) == 0);
		} else {
			ASSERT_TRUE(line.rfind("   ", 0) == 0);
			ASSERT_TRUE(line.rfind("1. ", 0) != 0);
		}
	}

	// Item 10: first line has "10. ", continuation lines have 4 spaces.
	ASSERT_TRUE(static_cast<std::string>(rows[idx_10]).rfind("10. ", 0) == 0);
	bool bad_10 = false;
	for (std::size_t i = idx_10 + 1; i < rows.size(); ++i) {
		const std::string line = static_cast<std::string>(rows[i]);
		if (line.empty())
			break; // paragraph terminator / trailing empty line
		if (line.rfind("    ", 0) != 0)
			bad_10 = true;
		if (line.rfind("10. ", 0) == 0)
			bad_10 = true;
	}
	if (bad_10) {
		std::cerr << "Unexpected prefix in reflow output:\n" << dump << "\n";
	}
	ASSERT_TRUE(!bad_10);

	// Debug helper if something goes wrong (kept as a string for easy inspection).
	EXPECT_TRUE(!to_string_rows(*buf).empty());
}
