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