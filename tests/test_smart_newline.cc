#include "Test.h"
#include "Buffer.h"
#include "Editor.h"
#include "Command.h"
#include <string>


TEST (SmartNewline_AutoIndent)
{
	Editor editor;
	InstallDefaultCommands();
	Buffer &buf = editor.Buffers().emplace_back();

	// Set up initial state: "  line1"
	buf.insert_text(0, 0, "  line1");
	buf.SetCursor(7, 0); // At end of line

	// Execute SmartNewline
	bool ok = Execute(editor, CommandId::SmartNewline);
	ASSERT_TRUE(ok);

	// Should have two lines now
	ASSERT_EQ(buf.Nrows(), 2);
	// Line 0 remains "  line1"
	ASSERT_EQ(buf.GetLineString(0), "  line1");
	// Line 1 should have "  " (two spaces)
	ASSERT_EQ(buf.GetLineString(1), "  ");
	// Cursor should be at (2, 1)
	ASSERT_EQ(buf.Curx(), 2);
	ASSERT_EQ(buf.Cury(), 1);
}


TEST (SmartNewline_TabIndent)
{
	Editor editor;
	InstallDefaultCommands();
	Buffer &buf = editor.Buffers().emplace_back();

	// Set up initial state: "\tline1"
	buf.insert_text(0, 0, "\tline1");
	buf.SetCursor(6, 0); // At end of line

	// Execute SmartNewline
	bool ok = Execute(editor, CommandId::SmartNewline);
	ASSERT_TRUE(ok);

	// Should have two lines now
	ASSERT_EQ(buf.Nrows(), 2);
	// Line 1 should have "\t"
	ASSERT_EQ(buf.GetLineString(1), "\t");
	// Cursor should be at (1, 1)
	ASSERT_EQ(buf.Curx(), 1);
	ASSERT_EQ(buf.Cury(), 1);
}


TEST (SmartNewline_NoIndent)
{
	Editor editor;
	InstallDefaultCommands();
	Buffer &buf = editor.Buffers().emplace_back();

	// Set up initial state: "line1"
	buf.insert_text(0, 0, "line1");
	buf.SetCursor(5, 0); // At end of line

	// Execute SmartNewline
	bool ok = Execute(editor, CommandId::SmartNewline);
	ASSERT_TRUE(ok);

	// Should have two lines now
	ASSERT_EQ(buf.Nrows(), 2);
	// Line 1 should be empty
	ASSERT_EQ(buf.GetLineString(1), "");
	// Cursor should be at (0, 1)
	ASSERT_EQ(buf.Curx(), 0);
	ASSERT_EQ(buf.Cury(), 1);
}