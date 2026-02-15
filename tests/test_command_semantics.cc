#include "Test.h"

#include "TestHarness.h"

using ktet::TestHarness;


TEST (CommandSemantics_KillToEOL_KillChain_And_Yank)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();

	b.insert_text(0, 0, std::string("abc\ndef"));
	b.SetCursor(1, 0); // a|bc

	ed.KillRingClear();
	ed.SetKillChain(false);

	ASSERT_TRUE(h.Exec(CommandId::KillToEOL));
	ASSERT_EQ(h.Text(), std::string("a\ndef"));
	ASSERT_EQ(ed.KillRingHead(), std::string("bc"));

	// At EOL, KillToEOL kills the newline (join).
	ASSERT_TRUE(h.Exec(CommandId::KillToEOL));
	ASSERT_EQ(h.Text(), std::string("adef"));
	ASSERT_EQ(ed.KillRingHead(), std::string("bc\n"));

	// Yank pastes the kill ring head and breaks the kill chain.
	ASSERT_TRUE(h.Exec(CommandId::Yank));
	ASSERT_EQ(h.Text(), std::string("abc\ndef"));
	ASSERT_EQ(ed.KillRingHead(), std::string("bc\n"));
	ASSERT_EQ(ed.KillChain(), false);
}


TEST (CommandSemantics_ToggleMark_JumpToMark)
{
	TestHarness h;
	Buffer &b = h.Buf();

	b.insert_text(0, 0, std::string("hello"));
	b.SetCursor(2, 0);
	ASSERT_EQ(b.MarkSet(), false);

	ASSERT_TRUE(h.Exec(CommandId::ToggleMark));
	ASSERT_EQ(b.MarkSet(), true);
	ASSERT_EQ(b.MarkCurx(), (std::size_t) 2);
	ASSERT_EQ(b.MarkCury(), (std::size_t) 0);

	b.SetCursor(4, 0);
	ASSERT_TRUE(h.Exec(CommandId::JumpToMark));
	ASSERT_EQ(b.Curx(), (std::size_t) 2);
	ASSERT_EQ(b.Cury(), (std::size_t) 0);
	// Jump-to-mark swaps: mark becomes previous cursor.
	ASSERT_EQ(b.MarkSet(), true);
	ASSERT_EQ(b.MarkCurx(), (std::size_t) 4);
	ASSERT_EQ(b.MarkCury(), (std::size_t) 0);
}


TEST (CommandSemantics_CtrlGRefresh_ClearsMark_WhenNothingElseToCancel)
{
	TestHarness h;
	Buffer &b = h.Buf();

	b.insert_text(0, 0, std::string("hello"));
	b.SetCursor(2, 0);
	ASSERT_EQ(b.MarkSet(), false);

	ASSERT_TRUE(h.Exec(CommandId::ToggleMark));
	ASSERT_EQ(b.MarkSet(), true);

	// C-g is mapped to Refresh; when there's no prompt/search/visual-line mode to cancel,
	// it should clear the mark.
	ASSERT_TRUE(h.Exec(CommandId::Refresh));
	ASSERT_EQ(b.MarkSet(), false);
}


TEST (CommandSemantics_CopyRegion_And_KillRegion)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();

	b.insert_text(0, 0, std::string("hello world"));
	b.SetCursor(0, 0);

	ed.KillRingClear();
	ed.SetKillChain(false);

	// Copy "hello" (region [0,5)).
	ASSERT_TRUE(h.Exec(CommandId::ToggleMark));
	b.SetCursor(5, 0);
	ASSERT_TRUE(h.Exec(CommandId::CopyRegion));
	ASSERT_EQ(ed.KillRingHead(), std::string("hello"));
	ASSERT_EQ(b.MarkSet(), false);
	ASSERT_EQ(h.Text(), std::string("hello world"));

	// Kill "world" (region [6,11)).
	ed.SetKillChain(false);
	b.SetCursor(6, 0);
	ASSERT_TRUE(h.Exec(CommandId::ToggleMark));
	b.SetCursor(11, 0);
	ASSERT_TRUE(h.Exec(CommandId::KillRegion));
	ASSERT_EQ(ed.KillRingHead(), std::string("world"));
	ASSERT_EQ(b.MarkSet(), false);
	ASSERT_EQ(h.Text(), std::string("hello "));
}