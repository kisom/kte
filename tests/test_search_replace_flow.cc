#include "Test.h"

#include "tests/TestHarness.h"

using ktet::TestHarness;

// These tests intentionally drive the prompt-based search/replace UI headlessly
// via `Execute(Editor&, CommandId, ...)` to lock down behavior without ncurses.

TEST(SearchFlow_FindStart_Success_LeavesCursorOnMatch_And_ClearsSearchState)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();

	b.insert_text(0, 0, "abc def abc");
	b.SetCursor(0, 0);
	b.SetOffsets(0, 0);

	// Keep a mark set to ensure search doesn't clobber it.
	b.SetMark(0, 0);
	ASSERT_TRUE(b.MarkSet());

	ASSERT_TRUE(h.Exec(CommandId::FindStart));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_EQ(ed.CurrentPromptKind(), Editor::PromptKind::Search);
	ASSERT_TRUE(ed.SearchActive());

	// Typing into the prompt uses InsertText and should jump to the first match.
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "def"));
	ASSERT_EQ(b.Cury(), (std::size_t) 0);
	ASSERT_EQ(b.Curx(), (std::size_t) 4);

	// Enter (Newline) accepts the prompt and ends incremental search.
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_TRUE(!ed.SearchActive());
	ASSERT_TRUE(b.MarkSet());
}


TEST(SearchFlow_FindStart_NotFound_RestoresOrigin_And_ClearsSearchState)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();

	b.insert_text(0, 0, "hello world\nsecond line\n");
	b.SetCursor(3, 0);
	b.SetOffsets(1, 2);

	const std::size_t ox   = b.Curx();
	const std::size_t oy   = b.Cury();
	const std::size_t orow = b.Rowoffs();
	const std::size_t ocol = b.Coloffs();

	ASSERT_TRUE(h.Exec(CommandId::FindStart));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_TRUE(ed.SearchActive());

	// Not-found should restore cursor/viewport to the saved origin while still in prompt.
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "zzzz"));
	ASSERT_EQ(b.Curx(), ox);
	ASSERT_EQ(b.Cury(), oy);
	ASSERT_EQ(b.Rowoffs(), orow);
	ASSERT_EQ(b.Coloffs(), ocol);

	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_TRUE(!ed.SearchActive());
}


TEST(SearchFlow_SearchReplace_EmptyFind_DoesNotMutateBuffer_And_ClearsState)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();

	b.insert_text(0, 0, "abc abc\n");
	b.SetCursor(0, 0);

	const std::string before = h.Text();

	ASSERT_TRUE(h.Exec(CommandId::SearchReplace));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_EQ(ed.CurrentPromptKind(), Editor::PromptKind::ReplaceFind);

	// Accept empty find -> proceed to ReplaceWith.
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_EQ(ed.CurrentPromptKind(), Editor::PromptKind::ReplaceWith);

	// Provide replacement and accept -> should cancel due to empty find.
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "X"));
	ASSERT_TRUE(h.Exec(CommandId::Newline));

	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_TRUE(!ed.SearchActive());
	ASSERT_EQ(h.Text(), before);
}


TEST(SearchFlow_SearchReplace_EmptyWith_ReplacesAdjacentOverlappingMatches)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();

	// "aaaa" with "aa" -> "" must remove both non-overlapping occurrences, not
	// just the first: after deleting the match at column 0, the next "aa" now
	// sits at column 0 too (not column 1), so the scan must resume at the
	// deletion point rather than one character past it.
	b.insert_text(0, 0, "aaaa\n");
	b.SetCursor(0, 0);

	ASSERT_TRUE(h.Exec(CommandId::SearchReplace));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_EQ(ed.CurrentPromptKind(), Editor::PromptKind::ReplaceFind);

	ASSERT_TRUE(h.Exec(CommandId::InsertText, "aa"));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_EQ(ed.CurrentPromptKind(), Editor::PromptKind::ReplaceWith);

	// Leave the replacement empty and accept.
	ASSERT_TRUE(h.Exec(CommandId::Newline));

	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));
}


TEST(SearchFlow_RegexFind_InvalidPattern_FailsSafely_And_ClearsStateOnEnter)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();

	b.insert_text(0, 0, "abc\ndef\n");
	b.SetCursor(1, 0);
	b.SetOffsets(0, 0);

	const std::size_t ox = b.Curx();
	const std::size_t oy = b.Cury();

	ASSERT_TRUE(h.Exec(CommandId::RegexFindStart));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_EQ(ed.CurrentPromptKind(), Editor::PromptKind::RegexSearch);

	// Invalid regex should not crash; cursor should remain at origin due to no matches.
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "("));
	ASSERT_EQ(b.Curx(), ox);
	ASSERT_EQ(b.Cury(), oy);

	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_TRUE(!ed.SearchActive());
}
