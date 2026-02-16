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


TEST (ReflowParagraph_IndentedBullets_PreserveStructure)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	// Test the example from the issue: indented list items should not be merged
	const std::string initial =
		"+ something at the top\n"
		"  + something indented\n"
		"+ the next line\n";
	b.insert_text(0, 0, initial);
	// Put cursor on first item
	b.SetCursor(0, 0);
	ed.AddBuffer(std::move(b));

	Buffer *buf = ed.CurrentBuffer();
	ASSERT_TRUE(buf != nullptr);

	// Use a width that's larger than all lines (so no wrapping should occur)
	const int width = 80;
	ASSERT_TRUE(Execute(ed, std::string("reflow-paragraph"), std::string(), width));

	const auto &rows         = buf->Rows();
	const std::string result = to_string_rows(*buf);

	// We should have 3 lines (plus possibly a trailing empty line)
	ASSERT_TRUE(rows.size() >= 3);

	// Check that the structure is preserved
	std::string line0 = static_cast<std::string>(rows[0]);
	std::string line1 = static_cast<std::string>(rows[1]);
	std::string line2 = static_cast<std::string>(rows[2]);

	// First line should start with "+ "
	EXPECT_TRUE(line0.rfind("+ ", 0) == 0);
	EXPECT_TRUE(line0.find("something at the top") != std::string::npos);

	// Second line should start with "  + " (two spaces, then +)
	EXPECT_TRUE(line1.rfind("  + ", 0) == 0);
	EXPECT_TRUE(line1.find("something indented") != std::string::npos);

	// Third line should start with "+ "
	EXPECT_TRUE(line2.rfind("+ ", 0) == 0);
	EXPECT_TRUE(line2.find("the next line") != std::string::npos);

	// The indented line should NOT be merged with the first line
	EXPECT_TRUE(line0.find("indented") == std::string::npos);

	// Debug output if something goes wrong
	if (line0.rfind("+ ", 0) != 0 || line1.rfind("  + ", 0) != 0 || line2.rfind("+ ", 0) != 0) {
		std::cerr << "Reflow did not preserve indented bullet structure:\n" << result << "\n";
	}
}