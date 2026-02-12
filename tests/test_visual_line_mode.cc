#include "Test.h"

#include "Buffer.h"
#include "Command.h"
#include "Editor.h"

#include <string>


static std::string
dump_buf(const Buffer &buf)
{
	std::string out;
	for (const auto &r: buf.Rows()) {
		out += static_cast<std::string>(r);
		out.push_back('\n');
	}
	return out;
}


static std::string
dump_bytes(const std::string &s)
{
	static const char *hex = "0123456789abcdef";
	std::string out;
	for (unsigned char c: s) {
		out.push_back(hex[(c >> 4) & 0xF]);
		out.push_back(hex[c & 0xF]);
		out.push_back(' ');
	}
	return out;
}


TEST (VisualLineMode_BroadcastInsert)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	b.insert_text(0, 0, "foo\nfoo\nfoo\n");
	b.SetCursor(1, 0); // fo|o
	ed.AddBuffer(std::move(b));

	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);

	// Enter visual-line mode and extend selection to 3 lines
	ASSERT_TRUE(Execute(ed, std::string("visual-line-toggle")));
	ASSERT_TRUE(Execute(ed, std::string("down"), std::string(), 2));

	// Broadcast insert to all selected lines
	ASSERT_TRUE(Execute(ed, std::string("insert"), std::string("X")));

	const std::string got = dump_buf(*ed.CurrentBuffer());
	// Note: buffers that end with a trailing '\n' have an extra empty row.
	const std::string exp = "fXoo\nfXoo\nfXoo\n\n";
	if (got != exp) {
		std::cerr << "Expected (len=" << exp.size() << ") bytes: " << dump_bytes(exp) << "\n";
		std::cerr << "Got      (len=" << got.size() << ") bytes: " << dump_bytes(got) << "\n";
	}
	ASSERT_TRUE(got == exp);
}


TEST (VisualLineMode_BroadcastInsert_UndoRedo)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	b.insert_text(0, 0, "foo\nfoo\nfoo\n");
	b.SetCursor(1, 0); // fo|o
	ed.AddBuffer(std::move(b));

	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);

	ASSERT_TRUE(Execute(ed, std::string("visual-line-toggle")));
	ASSERT_TRUE(Execute(ed, std::string("down"), std::string(), 2));

	// Broadcast insert to all selected lines.
	ASSERT_TRUE(Execute(ed, std::string("insert"), std::string("X")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		const std::string exp = "fXoo\nfXoo\nfXoo\n\n";
		ASSERT_TRUE(got == exp);
	}

	// Undo should restore all affected lines in a single step.
	ASSERT_TRUE(Execute(ed, std::string("undo")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		const std::string exp = "foo\nfoo\nfoo\n\n";
		ASSERT_TRUE(got == exp);
	}

	// Redo should re-apply the whole insert.
	ASSERT_TRUE(Execute(ed, std::string("redo")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		const std::string exp = "fXoo\nfXoo\nfXoo\n\n";
		ASSERT_TRUE(got == exp);
	}
}


TEST (VisualLineMode_BroadcastBackspace)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	b.insert_text(0, 0, "abcd\nabcd\nabcd\n");
	b.SetCursor(2, 0); // ab|cd
	ed.AddBuffer(std::move(b));

	ASSERT_TRUE(Execute(ed, std::string("visual-line-toggle")));
	ASSERT_TRUE(Execute(ed, std::string("down"), std::string(), 2));

	ASSERT_TRUE(Execute(ed, std::string("backspace")));
	const std::string got = dump_buf(*ed.CurrentBuffer());
	// Note: buffers that end with a trailing '\n' have an extra empty row.
	const std::string exp = "acd\nacd\nacd\n\n";
	if (got != exp) {
		std::cerr << "Expected (len=" << exp.size() << ") bytes: " << dump_bytes(exp) << "\n";
		std::cerr << "Got      (len=" << got.size() << ") bytes: " << dump_bytes(got) << "\n";
	}
	ASSERT_TRUE(got == exp);
}


TEST (VisualLineMode_BroadcastBackspace_UndoRedo)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	b.insert_text(0, 0, "abcd\nabcd\nabcd\n");
	b.SetCursor(2, 0); // ab|cd
	ed.AddBuffer(std::move(b));

	ASSERT_TRUE(Execute(ed, std::string("visual-line-toggle")));
	ASSERT_TRUE(Execute(ed, std::string("down"), std::string(), 2));

	ASSERT_TRUE(Execute(ed, std::string("backspace")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		const std::string exp = "acd\nacd\nacd\n\n";
		ASSERT_TRUE(got == exp);
	}

	// Undo should restore all affected lines.
	ASSERT_TRUE(Execute(ed, std::string("undo")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		const std::string exp = "abcd\nabcd\nabcd\n\n";
		ASSERT_TRUE(got == exp);
	}

	// Redo should re-apply.
	ASSERT_TRUE(Execute(ed, std::string("redo")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		const std::string exp = "acd\nacd\nacd\n\n";
		ASSERT_TRUE(got == exp);
	}
}


TEST (VisualLineMode_CancelWithCtrlG)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	b.insert_text(0, 0, "foo\nfoo\nfoo\n");
	b.SetCursor(1, 0);
	ed.AddBuffer(std::move(b));

	ASSERT_TRUE(Execute(ed, std::string("visual-line-toggle")));
	ASSERT_TRUE(Execute(ed, std::string("down"), std::string(), 2));

	// C-g is mapped to "refresh" and should cancel visual-line mode.
	ASSERT_TRUE(Execute(ed, std::string("refresh")));
	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	ASSERT_TRUE(!ed.CurrentBuffer()->VisualLineActive());

	// After cancel, edits should only affect the primary cursor line.
	ASSERT_TRUE(Execute(ed, std::string("insert"), std::string("X")));
	const std::string got = dump_buf(*ed.CurrentBuffer());
	// Cursor is still on the last line we moved to (down, down).
	const std::string exp = "foo\nfoo\nfXoo\n\n";
	if (got != exp) {
		std::cerr << "Expected (len=" << exp.size() << ") bytes: " << dump_bytes(exp) << "\n";
		std::cerr << "Got      (len=" << got.size() << ") bytes: " << dump_bytes(got) << "\n";
	}
	ASSERT_TRUE(got == exp);
}


TEST (Yank_ClearsMarkAndVisualLine)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	b.insert_text(0, 0, "foo\nbar\n");
	b.SetCursor(1, 0);
	ed.AddBuffer(std::move(b));

	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);
	Buffer *buf = ed.CurrentBuffer();

	// Seed mark + visual-line highlighting.
	buf->SetMark(buf->Curx(), buf->Cury());
	ASSERT_TRUE(buf->MarkSet());

	ASSERT_TRUE(Execute(ed, std::string("visual-line-toggle")));
	ASSERT_TRUE(Execute(ed, std::string("down"), std::string(), 1));
	ASSERT_TRUE(buf->VisualLineActive());

	// Yank should clear mark and any highlighting.
	ed.KillRingClear();
	ed.KillRingPush("X");
	ASSERT_TRUE(Execute(ed, std::string("yank")));

	ASSERT_TRUE(!buf->MarkSet());
	ASSERT_TRUE(!buf->VisualLineActive());
}


TEST (VisualLineMode_Yank_BroadcastsToBOL_AndUndo)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	b.insert_text(0, 0, "aa\nbb\ncc\n");
	b.SetCursor(1, 0); // a|a
	ed.AddBuffer(std::move(b));

	ASSERT_TRUE(ed.CurrentBuffer() != nullptr);

	// Enter visual-line mode and extend selection to 3 lines.
	ASSERT_TRUE(Execute(ed, std::string("visual-line-toggle")));
	ASSERT_TRUE(Execute(ed, std::string("down"), std::string(), 2));
	ASSERT_TRUE(ed.CurrentBuffer()->VisualLineActive());

	ed.KillRingClear();
	ed.KillRingPush("X");

	// Yank in visual-line mode should paste at BOL on every affected line.
	ASSERT_TRUE(Execute(ed, std::string("yank")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		// Note: buffers that end with a trailing '\n' have an extra empty row.
		const std::string exp = "Xaa\nXbb\nXcc\n\n";
		if (got != exp) {
			std::cerr << "Expected (len=" << exp.size() << ") bytes: " << dump_bytes(exp) << "\n";
			std::cerr << "Got      (len=" << got.size() << ") bytes: " << dump_bytes(got) << "\n";
		}
		ASSERT_TRUE(got == exp);
	}

	// Undo should restore all affected lines in a single step.
	ASSERT_TRUE(Execute(ed, std::string("undo")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		const std::string exp = "aa\nbb\ncc\n\n";
		if (got != exp) {
			std::cerr << "Expected (len=" << exp.size() << ") bytes: " << dump_bytes(exp) << "\n";
			std::cerr << "Got      (len=" << got.size() << ") bytes: " << dump_bytes(got) << "\n";
		}
		ASSERT_TRUE(got == exp);
	}

	// Redo should re-apply the whole yank.
	ASSERT_TRUE(Execute(ed, std::string("redo")));
	{
		const std::string got = dump_buf(*ed.CurrentBuffer());
		const std::string exp = "Xaa\nXbb\nXcc\n\n";
		ASSERT_TRUE(got == exp);
	}
}


TEST (VisualLineMode_Highlight_IsPerLineCursorSpot)
{
	Buffer b;
	// Note: buffers that end with a trailing '\n' have an extra empty row.
	b.insert_text(0, 0, "abcd\nx\nhi\n");
	// Place primary cursor on line 0 at column 3 (abc|d).
	b.SetCursor(3, 0);

	// Select lines 0..2 in visual-line mode.
	b.VisualLineStart();
	b.VisualLineSetActiveY(2);
	ASSERT_TRUE(b.VisualLineActive());
	ASSERT_TRUE(b.VisualLineStartY() == 0);
	ASSERT_TRUE(b.VisualLineEndY() == 2);

	// Line 0: "abcd" (len=4) => spot is 3
	ASSERT_TRUE(b.VisualLineSpotSelected(0, 3));
	ASSERT_TRUE(!b.VisualLineSpotSelected(0, 0));
	ASSERT_TRUE(!b.VisualLineSpotSelected(0, 2));
	ASSERT_TRUE(!b.VisualLineSpotSelected(0, 4));

	// Line 1: "x" (len=1) => spot clamps to EOL (1)
	ASSERT_TRUE(b.VisualLineSpotSelected(1, 1));
	ASSERT_TRUE(!b.VisualLineSpotSelected(1, 0));

	// Line 2: "hi" (len=2) => spot clamps to EOL (2)
	ASSERT_TRUE(b.VisualLineSpotSelected(2, 2));
	ASSERT_TRUE(!b.VisualLineSpotSelected(2, 0));

	// Outside the selected line range should never be highlighted.
	ASSERT_TRUE(!b.VisualLineSpotSelected(3, 0));
}