/*
 * test_audit_regressions.cc - Regression tests for defects recorded in
 * docs/audits/claude-findings-20260923.md. Each test names its finding ID.
 */
#include "Test.h"

#include "tests/TestHarness.h"

#include "ErrorRecovery.h"
#include "PieceTable.h"
#include "RegexGuard.h"
#include "Swap.h"
#include "UndoTree.h"
#include "syntax/HighlighterEngine.h"
#include "syntax/HighlighterRegistry.h"
#include "syntax/LanguageHighlighter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <random>
#include <set>
#include <vector>
#include <clocale>
#include <memory>
#include <string>
#include <thread>

using ktet::TestHarness;


// M1: assignment must not keep the destination's old line index.
TEST(Audit_M1_PieceTable_MoveAssign_ResetsLineIndex)
{
	const std::string a_text = "line1\nline2\nline3\n";
	PieceTable a;
	a.Append(a_text.data(), a_text.size());
	ASSERT_EQ(a.LineCount(), (std::size_t) 4); // builds a's index

	PieceTable b;
	b.Append("x", 1);
	a = std::move(b);

	ASSERT_EQ(a.Size(), (std::size_t) 1);
	ASSERT_EQ(a.LineCount(), (std::size_t) 1);
	auto [start, end] = a.GetLineRange(0);
	ASSERT_EQ(start, (std::size_t) 0);
	ASSERT_EQ(end, (std::size_t) 1);
}


TEST(Audit_M1_PieceTable_CopyAssign_ResetsLineIndex)
{
	const std::string a_text = "one\ntwo\nthree\n";
	PieceTable a;
	a.Append(a_text.data(), a_text.size());
	ASSERT_EQ(a.LineCount(), (std::size_t) 4);

	PieceTable b;
	b.Append("x", 1);
	a = b;

	ASSERT_EQ(a.LineCount(), (std::size_t) 1);
	ASSERT_EQ(a.GetLine(0), std::string("x"));
}


// B12: appends that merge into the last piece must invalidate the line index.
TEST(Audit_B12_PieceTable_MergedAppend_InvalidatesLineIndex)
{
	PieceTable t;
	t.Append("ab", 2);
	ASSERT_EQ(t.LineCount(), (std::size_t) 1);
	t.Append("\n", 1);
	ASSERT_EQ(t.LineCount(), (std::size_t) 2);
}


// M4: destroying a very long undo history must not recurse per node.
TEST(Audit_M4_UndoTree_DeepHistory_FreesWithoutRecursion)
{
	constexpr int kDepth = 1000000;
	auto *tree           = new UndoTree();
	UndoNode *prev       = nullptr;
	for (int i = 0; i < kDepth; ++i) {
		auto *n   = new UndoNode();
		n->parent = prev;
		if (prev)
			prev->child = n;
		else
			tree->root = n;
		prev = n;
	}
	tree->current = prev;
	delete tree; // stack overflow before the fix
	ASSERT_TRUE(true);
}


// D1: undoing a smart newline must restore the original text in one step.
TEST(Audit_D1_SmartNewline_UndoRestoresText)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "    foobar\nnext");
	b.SetCursor(7, 0);

	ASSERT_TRUE(h.Exec(CommandId::SmartNewline));
	ASSERT_EQ(h.Line(0), std::string("    foo"));
	ASSERT_EQ(h.Line(1), std::string("    bar"));
	ASSERT_EQ(b.Curx(), (std::size_t) 4);

	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("    foobar\nnext"));

	ASSERT_TRUE(h.Redo());
	ASSERT_EQ(h.Text(), std::string("    foo\n    bar\nnext"));
}


// B11: with a prompt open, SmartNewline accepts the prompt and edits nothing.
TEST(Audit_B11_SmartNewline_WithPrompt_DoesNotIndentBuffer)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();
	b.insert_text(0, 0, "    foo");
	b.SetCursor(7, 0);

	ASSERT_TRUE(h.Exec(CommandId::FindStart));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_TRUE(h.Exec(CommandId::SmartNewline));
	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_EQ(h.Text(), std::string("    foo"));
}


// D5: replace-all is a single undo step.
TEST(Audit_D5_ReplaceAll_SingleUndoStep)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();
	b.insert_text(0, 0, "a1 a2\na3");
	b.SetCursor(0, 0);

	ASSERT_TRUE(h.Exec(CommandId::SearchReplace));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "a"));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_EQ(ed.CurrentPromptKind(), Editor::PromptKind::ReplaceWith);
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "bb"));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_EQ(h.Text(), std::string("bb1 bb2\nbb3"));

	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("a1 a2\na3"));
}


// D2: scrolling that drags the cursor onto a shorter line clamps its column,
// so a following Backspace edits that line rather than joining lines.
TEST(Audit_D2_ScrollDown_ClampsCursorColumn)
{
	TestHarness h;
	Buffer &b = h.Buf();
	std::string text(50, 'x');
	for (int i = 0; i < 60; ++i)
		text += "\nab";
	b.insert_text(0, 0, text);
	b.SetCursor(40, 0);
	b.SetOffsets(0, 0);

	ASSERT_TRUE(h.Exec(CommandId::ScrollDown, std::string(), 5));
	ASSERT_EQ(b.Cury(), (std::size_t) 5);
	ASSERT_EQ(b.Curx(), (std::size_t) 2);

	const std::size_t rows_before = b.Nrows();
	ASSERT_TRUE(h.Exec(CommandId::Backspace));
	ASSERT_EQ(b.Nrows(), rows_before);
	ASSERT_EQ(h.Line(5), std::string("a"));
}


// M5: jumping to a mark that edits have left past the end clamps the cursor.
TEST(Audit_M5_JumpToMark_ClampsToBuffer)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "one\ntwo");
	b.SetMark(5, 40); // stale: no such row or column
	b.SetCursor(0, 0);

	ASSERT_TRUE(h.Exec(CommandId::JumpToMark));
	ASSERT_EQ(b.Cury(), (std::size_t) 1);
	ASSERT_EQ(b.Curx(), (std::size_t) 3);
}


// D6: reflow with the cursor on a blank separator line is a no-op.
TEST(Audit_D6_Reflow_OnBlankLine_KeepsParagraphsSeparate)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "aaa\n\nbbb");
	b.SetCursor(0, 1);

	ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph));
	ASSERT_EQ(h.Text(), std::string("aaa\n\nbbb"));
}


static bool
row_has_comment(Buffer &b, int row)
{
	const auto lh = b.Highlighter()->GetLine(b, row, b.Version());
	for (const auto &sp: lh.spans)
		if (sp.kind == kte::TokenKind::Comment)
			return true;
	return false;
}


// D9: undo back to the saved state must not leave stale highlighting (the
// rest of the file coloured as a comment after "/*" is undone).
TEST(Audit_D9_UndoToSaved_RefreshesHighlighting)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "int a;\nint b;\n");
	b.EnsureHighlighter();
	b.Highlighter()->SetHighlighter(kte::HighlighterRegistry::CreateFor("cpp"));
	if (auto *u = b.Undo())
		u->mark_saved();
	b.SetDirty(false);
	ASSERT_TRUE(!row_has_comment(b, 1));

	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "/*"));
	ASSERT_TRUE(row_has_comment(b, 1));

	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Line(0), std::string("int a;"));
	ASSERT_TRUE(!b.Dirty());
	ASSERT_TRUE(!row_has_comment(b, 1));
}


// P3: an edit keeps cached highlighting for the rows above it; only rows
// from the edit down are recomputed.
TEST(Audit_P3_EditKeepsHighlightCacheAbove)
{
	struct Counting : kte::StatefulHighlighter {
		int *calls;
		explicit Counting(int *c) : calls(c) {}

		void HighlightLine(const Buffer &, int, std::vector<kte::HighlightSpan> &) const override {}

		LineState HighlightLineStateful(const Buffer &, int, const LineState &prev,
		                                std::vector<kte::HighlightSpan> &) const override
		{
			++*calls;
			return prev;
		}
	};

	TestHarness h;
	Buffer &b = h.Buf();
	std::string text;
	for (int i = 0; i < 1000; ++i)
		text += "line\n";
	b.insert_text(0, 0, text);
	b.EnsureHighlighter();
	int calls = 0;
	b.Highlighter()->SetHighlighter(std::make_unique<Counting>(&calls));

	(void) b.Highlighter()->GetLine(b, 999, b.Version());
	ASSERT_EQ(calls, 1000);

	calls = 0;
	b.insert_text(990, 0, "x");
	(void) b.Highlighter()->GetLine(b, 999, b.Version());
	ASSERT_EQ(calls, 10); // rows 990..999, not the whole file
}


// D8: failures farther apart than the window must not accumulate.
TEST(Audit_D8_CircuitBreaker_WindowExpires)
{
	kte::CircuitBreaker::Config cfg;
	cfg.failure_threshold = 2;
	cfg.window            = std::chrono::seconds(1);
	kte::CircuitBreaker cb(cfg);

	cb.RecordFailure();
	std::this_thread::sleep_for(std::chrono::milliseconds(1100));
	cb.RecordFailure();

	ASSERT_TRUE(cb.GetState() == kte::CircuitBreaker::State::Closed);
	ASSERT_EQ(cb.GetFailureCount(), (std::size_t) 1);
}


// M3: an editor that shares another's buffers must attach them through the
// owner's SwapManager, so destroying it leaves no dangling recorders.
TEST(Audit_M3_SharedBuffers_UseOwnersSwapManager)
{
	TestHarness h;
	Editor &primary = h.EditorRef();

	auto secondary = std::make_unique<Editor>();
	secondary->SetSharedBuffers(&primary.Buffers(), primary.Swap());
	ASSERT_TRUE(secondary->Swap() == primary.Swap());
	secondary->AddBuffer(Buffer());
	const std::size_t idx = primary.Buffers().size() - 1;
	secondary.reset();

	// Edits record through the buffer's swap recorder; before the fix it
	// pointed into the destroyed secondary SwapManager.
	Buffer &b = primary.Buffers()[idx];
	b.insert_text(0, 0, "abc");
	ASSERT_EQ(b.GetLineString(0), std::string("abc"));
}


// D3: killing a region that ends on a last line without a trailing newline
// removes it entirely (no stray newline), and undo restores the original.
TEST(Audit_D3_KillRegion_ToUnterminatedLastLine)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "foo\nbar");
	b.SetMark(3, 0);
	b.SetCursor(3, 1);

	ASSERT_TRUE(h.Exec(CommandId::KillRegion));
	ASSERT_EQ(h.Text(), std::string("foo"));
	ASSERT_EQ(b.Nrows(), (std::size_t) 1);

	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("foo\nbar"));
}


// D3: killing the unterminated last line removes it and the newline before
// it; undo restores it exactly.
TEST(Audit_D3_KillLine_UnterminatedLastLine)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "a\nb");
	b.SetCursor(0, 1);

	ASSERT_TRUE(h.Exec(CommandId::KillLine));
	ASSERT_EQ(h.Text(), std::string("a"));
	ASSERT_EQ(h.EditorRef().KillRingHead(), std::string("b"));

	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("a\nb"));
}


// D4: the empty row after a trailing newline is not a line; killing it is a
// no-op, and undo must not add a line.
TEST(Audit_D4_KillLine_OnRowAfterTrailingNewline)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "a\n");
	b.SetCursor(0, 1);

	ASSERT_TRUE(h.Exec(CommandId::KillLine));
	ASSERT_EQ(h.Text(), std::string("a\n"));
	(void) h.Undo();
	ASSERT_EQ(h.Text(), std::string("a\n"));
}


static void
regex_replace_all(TestHarness &h, const std::string &find, const std::string &with)
{
	ASSERT_TRUE(h.Exec(CommandId::RegexpReplace));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, find));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	if (!with.empty())
		ASSERT_TRUE(h.Exec(CommandId::InsertText, with));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(!h.EditorRef().PromptActive());
}


// D4: a zero-width regex does not match the position after the final newline.
TEST(Audit_D4_RegexReplace_ZeroWidth_NoExtraLine)
{
	TestHarness h;
	h.Buf().insert_text(0, 0, "a\nb\n");
	regex_replace_all(h, "^", "# ");
	ASSERT_EQ(h.Text(), std::string("# a\n# b\n"));
}


// D3: regex replace on an unterminated last line does not add a newline.
TEST(Audit_D3_RegexReplace_KeepsMissingTrailingNewline)
{
	TestHarness h;
	h.Buf().insert_text(0, 0, "a\nfoo");
	regex_replace_all(h, "foo", "bar");
	ASSERT_EQ(h.Text(), std::string("a\nbar"));
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("a\nfoo"));
}


// D3: reflowing the last paragraph of a file without a trailing newline does
// not add one.
TEST(Audit_D3_Reflow_KeepsMissingTrailingNewline)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "one\ntwo");
	b.SetCursor(0, 1);
	ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph));
	ASSERT_EQ(h.Text(), std::string("one two"));
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("one\ntwo"));
}


// D5: the visual-line newline is undoable as one step.
TEST(Audit_D5_VisualLineNewline_Undo)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "ab\ncd\nef");
	b.SetCursor(1, 0);
	b.VisualLineStart();
	b.VisualLineSetActiveY(1);
	b.SetCursor(1, 1);

	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_EQ(h.Text(), std::string("a\nb\nc\nd\nef"));
	b.VisualLineClear();

	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("ab\ncd\nef"));
}


// B5: backspace and delete remove a whole UTF-8 character; undo restores it.
TEST(Audit_B5_Backspace_DeletesWholeUtf8Char)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "caf\xC3\xA9!");
	b.SetCursor(5, 0); // after the two-byte e-acute

	ASSERT_TRUE(h.Exec(CommandId::Backspace));
	ASSERT_EQ(h.Text(), std::string("caf!"));
	ASSERT_EQ(b.Curx(), (std::size_t) 3);

	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("caf\xC3\xA9!"));
}


TEST(Audit_B5_DeleteChar_DeletesWholeUtf8Char)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "a\xE4\xB8\xAD" "b"); // a, U+4E2D (3 bytes), b
	b.SetCursor(1, 0);

	ASSERT_TRUE(h.Exec(CommandId::DeleteChar));
	ASSERT_EQ(h.Text(), std::string("ab"));
}


TEST(Audit_B5_Motion_StepsOverUtf8Chars)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "x\xF0\x9F\x98\x80y"); // x, U+1F600 (4 bytes), y
	b.SetCursor(1, 0);

	ASSERT_TRUE(h.Exec(CommandId::MoveRight));
	ASSERT_EQ(b.Curx(), (std::size_t) 5);
	ASSERT_TRUE(h.Exec(CommandId::MoveLeft));
	ASSERT_EQ(b.Curx(), (std::size_t) 1);
}


// B6: horizontal scrolling counts display cells, not bytes, so the cursor
// at the end of a long CJK line stays on screen.
TEST(Audit_B6_HorizontalScroll_CountsCells)
{
	const char *prev = std::setlocale(LC_CTYPE, nullptr);
	const std::string saved = prev ? prev : "C";
	if (!std::setlocale(LC_CTYPE, "C.UTF-8") && !std::setlocale(LC_CTYPE, "C.utf8"))
		return; // no UTF-8 locale available

	TestHarness h;
	Buffer &b = h.Buf();
	std::string line;
	for (int i = 0; i < 100; ++i)
		line += "\xE4\xB8\xAD"; // U+4E2D, two cells wide
	b.insert_text(0, 0, line);
	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::MoveEnd));

	const std::size_t coloffs = b.Coloffs();
	std::setlocale(LC_CTYPE, saved.c_str());
	// 200 cells wide on an 80-column screen: the cursor cell is 200.
	ASSERT_EQ(coloffs, (std::size_t) (200 - 80 + 1));
}


// P2: the line index is updated in place by Insert/Delete. Query it after
// every edit (so it is never dirty) and compare with a plain string model.
static void
check_incremental_line_index(PieceTable &t)
{
	std::mt19937 rng(20260923u);
	std::string model;
	const char alphabet[] = "ab\nc\n\nd";
	for (int step = 0; step < 3000; ++step) {
		const bool do_insert = model.empty() || (rng() % 3 != 0);
		if (do_insert) {
			const std::size_t off = model.empty() ? 0 : rng() % (model.size() + 1);
			std::string text;
			const std::size_t n = 1 + rng() % 6;
			for (std::size_t i = 0; i < n; ++i)
				text.push_back(alphabet[rng() % (sizeof(alphabet) - 1)]);
			t.Insert(off, text.data(), text.size());
			model.insert(off, text);
		} else {
			const std::size_t off = rng() % model.size();
			const std::size_t n   = 1 + rng() % 5;
			t.Delete(off, n);
			model.erase(off, n);
		}
		// Reference line starts.
		std::vector<std::size_t> starts{0};
		for (std::size_t i = 0; i < model.size(); ++i)
			if (model[i] == '\n')
				starts.push_back(i + 1);
		ASSERT_EQ(t.LineCount(), starts.size());
		for (std::size_t l = 0; l < starts.size(); ++l) {
			const auto [s, e] = t.GetLineRange(l);
			ASSERT_EQ(s, starts[l]);
			ASSERT_EQ(e, (l + 1 < starts.size()) ? starts[l + 1] : model.size());
		}
	}
}


TEST(Audit_P2_IncrementalLineIndex_MatchesModel)
{
	PieceTable t;
	check_incremental_line_index(t);
}


// Consolidation (forced here by a tiny piece limit) rewrites pieces without
// changing content; the incrementally maintained index must stay correct.
TEST(Audit_P2_IncrementalLineIndex_WithConsolidation)
{
	PieceTable t(0, 8, 64, 1 << 20);
	check_incremental_line_index(t);
}


// A moved-from table must not keep (and incrementally update) the index of
// the text it gave away.
TEST(Audit_PieceTable_MovedFrom_LineIndexReset)
{
	const std::string text = "l1\nl2\nl3\n";
	PieceTable a;
	a.Append(text.data(), text.size());
	ASSERT_EQ(a.LineCount(), (std::size_t) 4);
	PieceTable b(std::move(a));
	a.Insert(0, "x", 1);
	ASSERT_EQ(a.Size(), (std::size_t) 1);
	ASSERT_EQ(a.LineCount(), (std::size_t) 1);
	ASSERT_EQ(b.LineCount(), (std::size_t) 4);
}


// Kill-line with a count stops at the end of the buffer instead of killing
// lines above the starting line.
TEST(Audit_KillLine_Count_StopsAtEndOfBuffer)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "a\nb\nc");
	b.SetCursor(0, 1);
	ASSERT_TRUE(h.Exec(CommandId::KillLine, std::string(), 3));
	ASSERT_EQ(h.Text(), std::string("a"));
}


// A mark left past the end of the buffer by edits is clamped before use:
// kill-region deletes the text between the clamped mark and the cursor.
TEST(Audit_KillRegion_StaleMarkIsClamped)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "abcdefgh");
	b.SetMark(1, 3); // no such row
	b.SetCursor(5, 0);
	ASSERT_TRUE(h.Exec(CommandId::KillRegion));
	ASSERT_EQ(h.Text(), std::string("abcde"));
	ASSERT_EQ(h.EditorRef().KillRingHead(), std::string("fgh"));
}


// After kill-region the cursor is on a real column, so typing then undoing
// removes exactly what was typed.
TEST(Audit_KillRegion_CursorClampedForUndo)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "worldond\nsecond\n");
	b.SetMark(11, 0); // past end of line 0 (the line was shortened)
	b.SetCursor(3, 1);
	ASSERT_TRUE(h.Exec(CommandId::KillRegion));
	ASSERT_EQ(h.Text(), std::string("worldondond\n"));
	ASSERT_EQ(b.Curx(), (std::size_t) 8);
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "Z"));
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("worldondond\n"));
}


// Undo groups nest: SmartNewline in visual-line mode (which groups the
// visual newline inside its own group) is a single undo step.
TEST(Audit_UndoGroups_Nest_SmartNewlineVisual)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "  ab\n  cd");
	b.SetCursor(3, 0);
	b.VisualLineStart();
	b.VisualLineSetActiveY(1);
	b.SetCursor(3, 1);
	ASSERT_TRUE(h.Exec(CommandId::SmartNewline));
	b.VisualLineClear();
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("  ab\n  cd"));
}


// Word motion and word deletion never stop inside a UTF-8 character.
TEST(Audit_WordMotion_Utf8)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "ab \xC3\xA9");
	b.SetCursor(5, 0);
	ASSERT_TRUE(h.Exec(CommandId::WordPrev));
	ASSERT_EQ(b.Curx(), (std::size_t) 3);
	b.SetCursor(5, 0);
	ASSERT_TRUE(h.Exec(CommandId::DeleteWordPrev));
	ASSERT_EQ(h.Text(), std::string("ab "));
}


// With a prompt open, buffer-editing commands do not run (they used to edit
// the buffer behind the prompt, skipping the read-only check).
TEST(Audit_Prompt_BlocksBufferCommands)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();
	b.insert_text(0, 0, "one\ntwo\n");
	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::FindStart));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_TRUE(h.Exec(CommandId::KillLine));
	ASSERT_TRUE(h.Exec(CommandId::DeleteChar));
	ASSERT_TRUE(h.Exec(CommandId::BufferNext));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_TRUE(ed.CurrentBuffer() == &b);
	ASSERT_TRUE(h.Exec(CommandId::Refresh)); // C-g still cancels
	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_EQ(h.Text(), std::string("one\ntwo\n"));
}


// The universal-argument count saturates instead of overflowing int.
TEST(Audit_UArg_DoesNotOverflow)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	ed.UArgStart();
	for (int i = 0; i < 12; ++i)
		ed.UArgDigit(9);
	ASSERT_EQ(ed.UArgGet(), 1000000);
	ed.UArgStart();
	for (int i = 0; i < 40; ++i)
		ed.UArgStart();
	ASSERT_EQ(ed.UArgGet(), 1000000);
}


// std::regex recurses per matched character. Regex search and replace on a
// very long line (minified JS/JSON) must not overflow the stack.
TEST(Audit_Regex_LongLine_NoStackOverflow)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();
	b.insert_text(0, 0, std::string(200000, 'a') + "\nshort\n");
	b.SetCursor(0, 1);

	ASSERT_TRUE(h.Exec(CommandId::RegexFindStart));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "(a|b)*"));
	ASSERT_TRUE(h.Exec(CommandId::Refresh));
	ASSERT_TRUE(!ed.PromptActive());

	regex_replace_all(h, "a+", "b");
	ASSERT_EQ(h.Line(0), std::string("b"));
	ASSERT_EQ(h.Line(1), std::string("short"));
}


namespace {
struct TempDir {
	std::filesystem::path path;


	explicit TempDir(const char *tag)
	{
		path = std::filesystem::temp_directory_path() /
		       (std::string("kte_ut_") + tag + "_" + std::to_string((int) ::getpid()));
		std::filesystem::remove_all(path);
		std::filesystem::create_directories(path);
	}


	~TempDir()
	{
		std::filesystem::remove_all(path);
	}
};


std::string
slurp(const std::filesystem::path &p)
{
	std::ifstream in(p, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace


// New files honour the umask (mkstemp's 0600 used to stick).
TEST(Audit_Save_NewFileHonoursUmask)
{
	TempDir d("save_umask");
	const mode_t old = ::umask(022);
	Buffer b;
	b.insert_text(0, 0, "x\n");
	std::string err;
	const bool ok = b.SaveAs((d.path / "new.txt").string(), err);
	::umask(old);
	ASSERT_TRUE(ok);
	struct stat st{};
	ASSERT_EQ(::stat((d.path / "new.txt").c_str(), &st), 0);
	ASSERT_EQ(st.st_mode & 0777, (mode_t) 0644);
}


// Save-as onto a symlink writes the target and keeps the link.
TEST(Audit_Save_ThroughSymlinkKeepsLink)
{
	TempDir d("save_symlink");
	std::ofstream(d.path / "target.txt") << "old\n";
	std::filesystem::create_symlink(d.path / "target.txt", d.path / "link.txt");
	Buffer b;
	b.insert_text(0, 0, "new\n");
	std::string err;
	ASSERT_TRUE(b.SaveAs((d.path / "link.txt").string(), err));
	ASSERT_TRUE(std::filesystem::is_symlink(d.path / "link.txt"));
	ASSERT_EQ(slurp(d.path / "target.txt"), std::string("new\n"));
}


// A file with several hard links is rewritten in place, so every name sees
// the new content.
TEST(Audit_Save_KeepsHardLinks)
{
	TempDir d("save_hardlink");
	std::ofstream(d.path / "a.txt") << "old\n";
	std::filesystem::create_hard_link(d.path / "a.txt", d.path / "b.txt");
	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile((d.path / "a.txt").string(), err));
	b.insert_text(0, 0, "new ");
	ASSERT_TRUE(b.Save(err));
	ASSERT_EQ(slurp(d.path / "b.txt"), std::string("new old\n"));
	ASSERT_EQ(std::filesystem::hard_link_count(d.path / "a.txt"), (std::uintmax_t) 2);
}


// Save-as from a file-backed buffer onto another existing file asks first.
TEST(Audit_SaveAs_ExistingFile_AsksFirst)
{
	TempDir d("saveas_confirm");
	std::ofstream(d.path / "mine.txt") << "mine\n";
	std::ofstream(d.path / "other.txt") << "precious\n";
	TestHarness h;
	Editor &ed = h.EditorRef();
	std::string err;
	ASSERT_TRUE(ed.OpenFile((d.path / "mine.txt").string(), err));
	ASSERT_TRUE(Execute(ed, "save-as", (d.path / "other.txt").string()));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_EQ(slurp(d.path / "other.txt"), std::string("precious\n"));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "n"));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_EQ(slurp(d.path / "other.txt"), std::string("precious\n"));
}


// Control characters are drawn as ^X (two cells); horizontal scrolling must
// count them the same way.
TEST(Audit_HorizontalScroll_ControlCharsAreTwoCells)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, std::string(50, '\r'));
	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::MoveEnd));
	ASSERT_EQ(b.Coloffs(), (std::size_t) (100 - 80 + 1));
}


// Opening a directory or a FIFO fails cleanly (a directory used to throw
// bad_alloc and exit the editor; a FIFO blocked forever).
TEST(Audit_Open_NonRegularFiles_Rejected)
{
	TempDir d("open_nonreg");
	Buffer b;
	std::string err;
	ASSERT_TRUE(!b.OpenFromFile(d.path.string(), err));
	ASSERT_TRUE(err.find("directory") != std::string::npos);
	const std::string fifo = (d.path / "pipe").string();
	ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
	err.clear();
	ASSERT_TRUE(!b.OpenFromFile(fifo, err));
}


// Saving never replaces a non-regular file (e.g. a device node or FIFO).
TEST(Audit_Save_RefusesNonRegularTarget)
{
	TempDir d("save_nonreg");
	const std::string fifo = (d.path / "pipe").string();
	ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
	Buffer b;
	b.insert_text(0, 0, "x\n");
	std::string err;
	ASSERT_TRUE(!b.SaveAs(fifo, err));
	struct stat st{};
	ASSERT_EQ(::lstat(fifo.c_str(), &st), 0);
	ASSERT_TRUE(S_ISFIFO(st.st_mode));
}


// Filesystem errors in commands (here: a name longer than NAME_MAX) are
// reported, not thrown out of the editor.
TEST(Audit_Commands_FilesystemErrorsDoNotThrow)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	h.Buf().insert_text(0, 0, "precious\n");
	const std::string longname = "/tmp/" + std::string(400, 'n');
	ASSERT_TRUE(Execute(ed, "save-as", longname) || true);
	ASSERT_TRUE(h.Exec(CommandId::OpenFileStart));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, longname));
	(void) h.Exec(CommandId::Newline);
	(void) ed.ProcessPendingOpens();
	ASSERT_EQ(h.Text(), std::string("precious\n"));
}


// Renderer regex highlighting on a long line with a deeply recursive pattern
// (overflowed an 8 MiB stack at under 10,000 bytes) runs off the main stack.
TEST(Audit_ForEachRegexMatch_LongLine)
{
	std::string line;
	while (line.size() < 200000)
		line += "word, more words; and so on. ";
	std::size_t n = 0, total = 0;
	kte::ForEachRegexMatch(line, std::regex("(\\w|\\s|[.,;])+"), [&](std::size_t, std::size_t len) {
		++n;
		total += len;
	});
	ASSERT_EQ(n, (std::size_t) 1);
	ASSERT_EQ(total, line.size());
}


// Quit checks every buffer for unsaved changes, not just the current one.
TEST(Audit_Quit_ChecksAllBuffers)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	h.Buf().insert_text(0, 0, "unsaved");
	h.Buf().SetDirty(true);
	Buffer other;
	ed.AddBuffer(std::move(other));
	ed.SwitchTo(ed.BufferCount() - 1);
	ASSERT_TRUE(!ed.CurrentBuffer()->Dirty());
	ASSERT_TRUE(h.Exec(CommandId::Quit));
	ASSERT_TRUE(!ed.QuitRequested());
	ASSERT_TRUE(h.Exec(CommandId::Quit));
	ASSERT_TRUE(ed.QuitRequested());
}


// Reload refuses a file deleted on disk and asks before discarding edits.
TEST(Audit_Reload_ConfirmsAndRefusesDeletedFile)
{
	TempDir d("reload_safety");
	const std::string file = (d.path / "r.txt").string();
	std::ofstream(file) << "disk\n";
	TestHarness h;
	Editor &ed = h.EditorRef();
	std::string err;
	ASSERT_TRUE(ed.OpenFile(file, err));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "EDIT "));
	ASSERT_TRUE(h.Exec(CommandId::ReloadBuffer));
	ASSERT_EQ(h.Line(0), std::string("EDIT disk"));
	std::filesystem::remove(file);
	(void) h.Exec(CommandId::ReloadBuffer);
	(void) h.Exec(CommandId::ReloadBuffer);
	ASSERT_EQ(h.Line(0), std::string("EDIT disk"));
}


// Closing a dirty buffer: Enter alone at "Save changes?" cancels instead of
// discarding the edits.
TEST(Audit_CloseConfirm_EmptyAnswerCancels)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	h.Buf().insert_text(0, 0, "work");
	h.Buf().SetDirty(true);
	const std::size_t n = ed.BufferCount();
	ASSERT_TRUE(h.Exec(CommandId::BufferClose));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_EQ(ed.BufferCount(), n);
	ASSERT_EQ(h.Text(), std::string("work"));
}


// Save-as onto a file that is open in another buffer is refused (it would
// leave two buffers, and one journal, for the same file).
TEST(Audit_SaveAs_FileOpenInOtherBuffer_Refused)
{
	TempDir d("saveas_open");
	std::ofstream(d.path / "y.txt") << "y\n";
	std::ofstream(d.path / "z.txt") << "z\n";
	TestHarness h;
	Editor &ed = h.EditorRef();
	std::string err;
	ASSERT_TRUE(ed.OpenFile((d.path / "y.txt").string(), err));
	ASSERT_TRUE(ed.OpenFile((d.path / "z.txt").string(), err));
	const std::size_t n = ed.BufferCount();
	(void) Execute(ed, "save-as", (d.path / "y.txt").string());
	ASSERT_TRUE(!ed.PromptActive());
	ASSERT_EQ(slurp(d.path / "y.txt"), std::string("y\n"));
	ASSERT_EQ(ed.BufferCount(), n);
	ASSERT_EQ(ed.FindOpenBuffer((d.path / "y.txt").string()) != ed.CurrentBufferIndex(), true);
}


// Saving keeps setuid/setgid bits (chown after chmod used to clear them).
TEST(Audit_Save_KeepsSpecialModeBits)
{
	TempDir d("save_setuid");
	const auto f = d.path / "s.sh";
	std::ofstream(f) << "echo\n";
	ASSERT_EQ(::chmod(f.c_str(), 06755), 0);
	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile(f.string(), err));
	b.insert_text(0, 0, "#");
	ASSERT_TRUE(b.Save(err));
	struct stat st{};
	ASSERT_EQ(::stat(f.c_str(), &st), 0);
	ASSERT_EQ(st.st_mode & 07777, (mode_t) 06755);
}


// The help buffer's name is not a path: it is not journaled, and saving it
// asks for a file name instead of writing ./+HELP+.
TEST(Audit_HelpBuffer_NotJournaledOrSavedByName)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	ASSERT_TRUE(h.Exec(CommandId::ShowHelp));
	Buffer *help = ed.CurrentBuffer();
	ASSERT_TRUE(help->IsVirtual());
	ASSERT_TRUE(h.Exec(CommandId::ToggleReadOnly));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "x"));
	ed.Swap()->Flush(help);
	ASSERT_TRUE(!std::filesystem::exists(kte::SwapManager::ComputeSwapPathForTests(*help)));
	ASSERT_TRUE(h.Exec(CommandId::Save));
	ASSERT_TRUE(ed.PromptActive());
	ASSERT_TRUE(ed.CurrentPromptKind() == Editor::PromptKind::SaveAs);
	ASSERT_TRUE(!std::filesystem::exists("+HELP+"));
	ASSERT_TRUE(h.Exec(CommandId::Refresh));
}


// A huge repeated yank is refused rather than allocating without bound.
TEST(Audit_Yank_HugeRepeatRefused)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	ed.KillRingPush(std::string(1 << 20, 'k'));
	ASSERT_TRUE(!h.Exec(CommandId::Yank, std::string(), 1000000));
	ASSERT_EQ(h.Buf().Nrows(), (std::size_t) 1);
	ASSERT_EQ(h.Line(0), std::string());
}


// Undoing a multi-line kill puts the cursor after the restored text (it was
// placed at col + size on the start row, past the end of the line, so later
// edits and their undo went to the wrong place).
TEST(Audit_Undo_MultiLineRestore_CursorAfterText)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "a\nb\n");
	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::ToggleMark));
	ASSERT_TRUE(h.Exec(CommandId::MoveDown));
	ASSERT_TRUE(h.Exec(CommandId::KillRegion));
	ASSERT_EQ(h.Text(), std::string("b\n"));
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("a\nb\n"));
	ASSERT_EQ(b.Cury(), (std::size_t) 1);
	ASSERT_EQ(b.Curx(), (std::size_t) 0);
	ASSERT_TRUE(h.Exec(CommandId::KillLine));
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("a\nb\n"));
}


// Same shape that overflowed the heap in delete-word-prev: after the undo
// the cursor is within its line.
TEST(Audit_Undo_ThenDeleteWordPrev_NoOverflow)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, std::string(24, 'a') + "\n" + std::string(30, 'b'));
	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::ToggleMark));
	ASSERT_TRUE(h.Exec(CommandId::MoveDown));
	ASSERT_TRUE(h.Exec(CommandId::MoveEnd));
	ASSERT_TRUE(h.Exec(CommandId::KillRegion));
	ASSERT_TRUE(h.Undo());
	ASSERT_TRUE(b.Curx() <= h.Line(b.Cury()).size());
	ASSERT_TRUE(h.Exec(CommandId::DeleteWordPrev));
}


// Save-and-quit records the saved state, so undo afterwards marks the buffer
// dirty again (it differs from the file).
TEST(Audit_SaveAndQuit_MarksSaved)
{
	TempDir d("saq_marksaved");
	std::ofstream(d.path / "a.txt") << "aaa\n";
	TestHarness h;
	Editor &ed = h.EditorRef();
	h.Buf().insert_text(0, 0, "dirty other");
	h.Buf().SetDirty(true);
	std::string err;
	ASSERT_TRUE(ed.OpenFile((d.path / "a.txt").string(), err));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "X"));
	ASSERT_TRUE(h.Exec(CommandId::SaveAndQuit));
	ASSERT_TRUE(!ed.QuitRequested()); // other buffer still dirty
	ASSERT_EQ(slurp(d.path / "a.txt"), std::string("Xaaa\n"));
	ASSERT_TRUE(!ed.CurrentBuffer()->Dirty());
	ASSERT_TRUE(h.Undo());
	ASSERT_TRUE(ed.CurrentBuffer()->Dirty());
}


// Visual-line newline with a stale selection never puts the cursor past the
// buffer, so later typing is undoable.
TEST(Audit_VisualLine_StaysInsideBuffer)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "one\ntwo\nthree\n");
	b.SetCursor(0, 0);
	const std::string orig = h.Text();
	ASSERT_TRUE(h.Exec(CommandId::VisualLineModeToggle));
	ASSERT_TRUE(h.Exec(CommandId::MarkAllAndJumpEnd));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(h.Exec(CommandId::VisualLineModeToggle));
	ASSERT_TRUE(b.Cury() < b.Nrows());
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "Z"));
	for (int i = 0; i < 10; ++i)
		(void) h.Undo();
	ASSERT_EQ(h.Text(), orig);
}


// A leftover copy from an interrupted hard-link save does not block saving.
TEST(Audit_Save_HardLink_LeftoverCopyDoesNotBlock)
{
	TempDir d("save_leftover");
	std::ofstream(d.path / "h.txt") << "old\n";
	std::filesystem::create_hard_link(d.path / "h.txt", d.path / "h2.txt");
	std::ofstream(d.path / "h.txt.kte-save") << "stale\n";
	Buffer b;
	std::string err;
	ASSERT_TRUE(b.OpenFromFile((d.path / "h.txt").string(), err));
	b.insert_text(0, 0, "new ");
	ASSERT_TRUE(b.Save(err));
	ASSERT_EQ(slurp(d.path / "h2.txt"), std::string("new old\n"));
}


// A virtual buffer saved under a real name is journaled from then on.
TEST(Audit_VirtualBuffer_SavedAs_GetsJournal)
{
	TempDir d("virtual_saveas");
	TestHarness h;
	Editor &ed = h.EditorRef();
	ASSERT_TRUE(h.Exec(CommandId::ShowHelp));
	ASSERT_TRUE(h.Exec(CommandId::ToggleReadOnly));
	const std::string target = (d.path / "notes.txt").string();
	ASSERT_TRUE(Execute(ed, "save-as", target));
	Buffer *b = ed.CurrentBuffer();
	ASSERT_TRUE(!b->IsVirtual());
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "x"));
	ed.Swap()->Flush(b);
	const std::string swp = kte::SwapManager::ComputeSwapPathForTests(*b);
	ASSERT_TRUE(std::filesystem::exists(swp));
	std::filesystem::remove(swp);
}


// Replace-all is applied as one edit; results and undo are unchanged.
TEST(Audit_ReplaceAll_WholeBuffer_ResultAndUndo)
{
	TestHarness h;
	Buffer &b = h.Buf();
	const std::string orig = "cat cat\ndog\ncatcat\n";
	b.insert_text(0, 0, orig);
	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::SearchReplace));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "cat"));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "ox"));
	ASSERT_TRUE(h.Exec(CommandId::Newline));
	ASSERT_EQ(h.Text(), std::string("ox ox\ndog\noxox\n"));
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), orig);
	ASSERT_TRUE(h.Redo());
	ASSERT_EQ(h.Text(), std::string("ox ox\ndog\noxox\n"));
}


// Yanking a multi-line kill inserts it in one edit and leaves the cursor
// after it.
TEST(Audit_Yank_MultiLine_SingleInsert)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	Buffer &b  = h.Buf();
	b.insert_text(0, 0, "AB");
	b.SetCursor(1, 0);
	ed.KillRingPush("x\ny\nz");
	ASSERT_TRUE(h.Exec(CommandId::Yank));
	ASSERT_EQ(h.Text(), std::string("Ax\ny\nzB"));
	ASSERT_EQ(b.Cury(), (std::size_t) 2);
	ASSERT_EQ(b.Curx(), (std::size_t) 1);
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("AB"));
}


// A repeat count inserts the repeated text once; undo removes all of it.
TEST(Audit_InsertText_RepeatCount)
{
	TestHarness h;
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "ab", 3));
	ASSERT_EQ(h.Text(), std::string("ababab"));
	ASSERT_EQ(h.Buf().Curx(), (std::size_t) 6);
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string(""));
}


// A reload confirmation is cancelled by any other command, so a later C-k l
// (after, say, moving the cursor) asks again instead of discarding edits.
TEST(Audit_Reload_ConfirmationCancelledByOtherCommand)
{
	TempDir d("reload_cancel");
	const std::string file = (d.path / "r.txt").string();
	std::ofstream(file) << "disk\n";
	TestHarness h;
	Editor &ed = h.EditorRef();
	std::string err;
	ASSERT_TRUE(ed.OpenFile(file, err));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "EDIT "));
	ASSERT_TRUE(h.Exec(CommandId::ReloadBuffer));
	(void) h.Exec(CommandId::MoveRight);
	ASSERT_TRUE(h.Exec(CommandId::ReloadBuffer));
	ASSERT_EQ(h.Line(0), std::string("EDIT disk"));
	ASSERT_TRUE(h.Exec(CommandId::ReloadBuffer));
	ASSERT_EQ(h.Line(0), std::string("disk"));
}


// Buffer ids: copies get a fresh id, moves keep it, so caches keyed on the
// id never mistake a new buffer at a reused address for the old one.
TEST(Audit_BufferId_StableAcrossMovesUniqueForCopies)
{
	Buffer a;
	const std::uint64_t id = a.Id();
	ASSERT_TRUE(id != 0);
	Buffer b(a);
	ASSERT_TRUE(b.Id() != id);
	Buffer c(std::move(a));
	ASSERT_EQ(c.Id(), id);
	Buffer e;
	e = std::move(c);
	ASSERT_EQ(e.Id(), id);
	Buffer f;
	f = e;
	ASSERT_TRUE(f.Id() != id);
}


// Replace-all records only the changed span in the undo tree (it recorded
// the whole buffer twice per run), and undo/redo still restore the text,
// including when the change sits next to multibyte characters.
TEST(Audit_ReplaceAll_RecordsOnlyChangedSpan)
{
	TestHarness h;
	const std::string big(100000, 'x');
	const std::string before = big + "\nh\xC3\xA9llo w\xC3\xB8rld\n" + big + "\n";
	h.Buf().insert_text(0, 0, before);
	h.Buf().SetCursor(0, 0);
	regex_replace_all(h, "\xC3\xB8", "\xC3\xA5");
	const std::string after = big + "\nh\xC3\xA9llo w\xC3\xA5rld\n" + big + "\n";
	ASSERT_EQ(h.Text(), after);
	const UndoNode *cur = h.Buf().Undo()->TreeForTests().current;
	ASSERT_TRUE(cur != nullptr);
	ASSERT_TRUE(cur->text.size() < 16);
	ASSERT_TRUE(cur->parent != nullptr && cur->parent->text.size() < 16);
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), before);
	ASSERT_TRUE(h.Redo());
	ASSERT_EQ(h.Text(), after);

	// Growing, and splitting lines.
	regex_replace_all(h, "rld", "rld!\n");
	regex_replace_all(h, "o w", "o\nw");
	const std::string after2 = big + "\nh\xC3\xA9llo\nw\xC3\xA5rld!\n\n" + big + "\n";
	ASSERT_EQ(h.Text(), after2);
	ASSERT_TRUE(h.Undo());
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), after);
	ASSERT_TRUE(h.Redo());
	ASSERT_TRUE(h.Redo());
	ASSERT_EQ(h.Text(), after2);
}


// Search-as-you-type skips very long lines (std::regex can be quadratic in
// line length) but says so, and moving to the next match searches them.
TEST(Audit_RegexSearch_LongLineFoundByNext)
{
	TestHarness h;
	Editor &ed = h.EditorRef();
	h.Buf().insert_text(0, 0, "short\n" + std::string(25000, 'a') + "NEEDLE\nend\n");
	h.Buf().SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::RegexFindStart));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "NEE+DLE"));
	ASSERT_TRUE(ed.Status().find("long line") != std::string::npos);
	ASSERT_TRUE(h.Exec(CommandId::MoveRight));
	ASSERT_EQ(h.Buf().Cury(), (std::size_t) 1);
	ASSERT_EQ(h.Buf().Curx(), (std::size_t) 25000);
	ASSERT_TRUE(h.Exec(CommandId::Refresh));
	ASSERT_TRUE(!ed.PromptActive());
	// The replace prompt has no next/previous; its Enter searches all lines.
	ASSERT_TRUE(h.Exec(CommandId::RegexpReplace));
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "NEE+DLE"));
	ASSERT_TRUE(ed.Status().find("Enter searches all") != std::string::npos);
}


// Replace-all against a per-line std::regex_replace model, with overlapping
// common prefixes and suffixes; undo restores the original each time.
TEST(Audit_ReplaceAll_TrimmedEditMatchesModel)
{
	const char *texts[] = {"aaa\nbab\n", "abc", "\xC3\xA9\xC3\xA9x\xC3\xA9\n\n", "aXa\naa\n", "\n\n"};
	const std::pair<const char *, const char *> reps[] = {
		{"a", ""}, {"a", "aa"}, {"b", "a"}, {"^", "a"}, {"X", "aXa"}, {"a$", ""}, {"x", "\xC3\xA9"},
		{"\xC3\xA9", "e"}, {"aa", "a"}, {"^$", "a"}
	};
	for (const char *t: texts) {
		for (const auto &[find, with]: reps) {
			TestHarness h;
			h.Buf().insert_text(0, 0, t);
			h.Buf().SetCursor(0, 0);
			std::string expect;
			const std::regex rx(find);
			std::string src(t);
			std::size_t start = 0;
			while (true) {
				const std::size_t nl = src.find('\n', start);
				if (start == src.size() && start > 0)
					break; // no line after a trailing newline
				const std::string line = src.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
				expect += std::regex_replace(line, rx, with);
				if (nl == std::string::npos)
					break;
				expect += '\n';
				start = nl + 1;
			}
			regex_replace_all(h, find, with);
			if (h.Text() != expect)
				fprintf(stderr, "text=[%s] find=[%s] with=[%s] got=[%s] want=[%s]\n", t, find, with,
				        h.Text().c_str(), expect.c_str());
			ASSERT_EQ(h.Text(), expect);
			if (expect != t) {
				ASSERT_TRUE(h.Undo());
				ASSERT_EQ(h.Text(), std::string(t));
				ASSERT_TRUE(h.Redo());
				ASSERT_EQ(h.Text(), expect);
			}
		}
	}
}


// insert_row (undo of C-k C-k) sent its text and newline as two journal
// records; a checkpoint triggered by the first (here the 1 MiB threshold)
// already held the newline, which the second then added again on replay.
TEST(Audit_Swap_InsertRowIsOneRecord)
{
	TempDir d("insert_row_swap");
	const std::string path = (d.path / "big.txt").string();
	{
		std::ofstream o(path, std::ios::binary | std::ios::trunc);
		o << std::string(1100000, 'A') << "\nB\n";
	}
	std::remove(kte::SwapManager::ComputeSwapPathForFilename(path).c_str());
	TestHarness h;
	std::string err;
	ASSERT_TRUE(h.EditorRef().OpenFile(path, err));
	Buffer &b = h.Buf();
	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::KillLine));
	ASSERT_TRUE(h.Exec(CommandId::Undo));
	const std::string text = b.BytesForTests();
	h.EditorRef().Swap()->Flush(&b);
	const std::string copy = (d.path / "crash.swp").string();
	{
		std::ofstream o(copy, std::ios::binary | std::ios::trunc);
		o << slurp(kte::SwapManager::ComputeSwapPathForTests(b));
	}
	Buffer b2;
	ASSERT_TRUE(b2.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b2, copy, err));
	ASSERT_TRUE(b2.BytesForTests() == text);
	b.SetDirty(false);
}


// Visual-line yank with the cursor moved outside the selection (C-k a)
// left it past the end of an empty row; text typed there could not be
// undone. Every command now leaves the cursor inside the buffer.
TEST(Audit_VisualYank_CursorStaysInBuffer)
{
	TestHarness h;
	Buffer &b = h.Buf();
	b.insert_text(0, 0, "abc def\n");
	b.SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::DeleteWordNext));
	ASSERT_TRUE(h.Exec(CommandId::VisualLineModeToggle));
	ASSERT_TRUE(h.Exec(CommandId::MarkAllAndJumpEnd));
	ASSERT_TRUE(h.Exec(CommandId::Yank));
	ASSERT_TRUE(b.Curx() <= b.GetLineString(b.Cury()).size());
	ASSERT_TRUE(b.Cury() < b.Nrows());
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "Z"));
	ASSERT_TRUE(h.Exec(CommandId::Backspace));
	for (int i = 0; i < 20; ++i)
		(void) h.Exec(CommandId::Undo);
	ASSERT_EQ(h.Text(), std::string("abc def\n"));
}


// Visual-line edits apply at the cursor's column on every selected line; a
// byte column taken from an ASCII line fell inside the CJK characters of
// the next line and split them. The column is now carried in characters.
TEST(Audit_VisualLine_EditsKeepUtf8Whole)
{
	const std::string cjk = "\xE4\xB8\x96\xE7\x95\x8C"; // two 3-byte characters
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "abcdef\n" + cjk + "\n");
		h.Buf().SetCursor(0, 1);
		ASSERT_TRUE(h.Exec(CommandId::VisualLineModeToggle));
		ASSERT_TRUE(h.Exec(CommandId::MoveUp));
		ASSERT_TRUE(h.Exec(CommandId::MoveRight));
		ASSERT_TRUE(h.Exec(CommandId::InsertText, "X"));
		ASSERT_EQ(h.Text(), std::string("aXbcdef\n\xE4\xB8\x96X\xE7\x95\x8C\n"));
	}
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "abcdef\n" + cjk + "\n");
		h.Buf().SetCursor(0, 1);
		ASSERT_TRUE(h.Exec(CommandId::VisualLineModeToggle));
		ASSERT_TRUE(h.Exec(CommandId::MoveUp));
		ASSERT_TRUE(h.Exec(CommandId::DeleteChar));
		ASSERT_EQ(h.Text(), std::string("bcdef\n\xE7\x95\x8C\n"));
		ASSERT_TRUE(h.Exec(CommandId::MoveRight));
		ASSERT_TRUE(h.Exec(CommandId::Backspace));
		ASSERT_EQ(h.Text(), std::string("cdef\n\n"));
	}
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "abcdef\n" + cjk + "\n");
		h.Buf().SetCursor(0, 1);
		ASSERT_TRUE(h.Exec(CommandId::VisualLineModeToggle));
		ASSERT_TRUE(h.Exec(CommandId::MoveUp));
		ASSERT_TRUE(h.Exec(CommandId::MoveRight));
		ASSERT_TRUE(h.Exec(CommandId::Newline));
		ASSERT_EQ(h.Text(), std::string("a\nbcdef\n\xE4\xB8\x96\n\xE7\x95\x8C\n"));
	}
}


// A session that cannot journal its buffer (another kte holds the journal,
// or writes fail) now says so in the status line; it used to go only to
// the error log, leaving crash recovery silently off.
TEST(Audit_Swap_FailuresReachTheUser)
{
	TempDir d("swap_notice");
	const std::string path = (d.path / "f.txt").string();
	std::ofstream(path) << "base\n";
	const std::string swp = kte::SwapManager::ComputeSwapPathForFilename(path);
	std::filesystem::remove_all(swp);

	// Session A holds the journal; session B is locked out.
	Buffer a, b;
	std::string err;
	ASSERT_TRUE(a.OpenFromFile(path, err));
	ASSERT_TRUE(b.OpenFromFile(path, err));
	kte::SwapManager sa, sb;
	sa.Attach(&a);
	a.SetSwapRecorder(sa.RecorderFor(&a));
	a.insert_text(0, 0, "A");
	sa.Flush(&a);
	ASSERT_EQ(sa.TakeUserNotice(), std::string());
	sb.Attach(&b);
	b.SetSwapRecorder(sb.RecorderFor(&b));
	b.insert_text(0, 0, "B");
	sb.Flush(&b);
	ASSERT_TRUE(sb.TakeUserNotice().find("another kte") != std::string::npos);
	ASSERT_EQ(sb.TakeUserNotice(), std::string());
	// Saving resets the journal and retries the lock; still held by A, but
	// the user was already told.
	sb.ResetJournal(b);
	b.insert_text(0, 0, "b");
	sb.Flush(&b);
	ASSERT_EQ(sb.TakeUserNotice(), std::string());
	b.SetSwapRecorder(nullptr);
	sb.Detach(&b, false);
	a.SetSwapRecorder(nullptr);
	sa.Detach(&a, true);

	// Writes fail: told once, not per keystroke.
	std::filesystem::remove_all(swp);
	std::filesystem::create_directories(swp);
	Buffer c;
	ASSERT_TRUE(c.OpenFromFile(path, err));
	kte::SwapManager sc;
	sc.Attach(&c);
	c.SetSwapRecorder(sc.RecorderFor(&c));
	c.insert_text(0, 0, "C");
	c.insert_text(0, 1, "D");
	sc.Flush(&c);
	ASSERT_TRUE(sc.TakeUserNotice().find("journal write failed") != std::string::npos);
	ASSERT_EQ(sc.TakeUserNotice(), std::string());
	c.SetSwapRecorder(nullptr);
	sc.Detach(&c, false);
	std::filesystem::remove_all(swp);
}


// A file whose basename is too long for the encoded swap name fell back to
// "<basename>.<hash>.swp", which exceeded NAME_MAX: no journal at all.
TEST(Audit_Swap_LongBasenameStillJournaled)
{
	TempDir d("swap_longname");
	const std::string path = (d.path / (std::string(240, 'n') + ".txt")).string();
	std::ofstream(path) << "base\n";
	const std::string swp = kte::SwapManager::ComputeSwapPathForFilename(path);
	// Room for compaction's "<journal>.tmp" too.
	ASSERT_TRUE(std::filesystem::path(swp).filename().string().size() + 4 <= 255);
	std::filesystem::remove(swp);
	Buffer a;
	std::string err;
	ASSERT_TRUE(a.OpenFromFile(path, err));
	kte::SwapManager sm;
	kte::SwapConfig cfg;
	cfg.checkpoint_bytes = 100;
	cfg.compact_bytes    = 1000;
	sm.SetConfig(cfg);
	sm.Attach(&a);
	a.SetSwapRecorder(sm.RecorderFor(&a));
	for (int i = 0; i < 50; ++i)
		a.insert_text(0, 0, std::string(200, 'X'));
	sm.Flush(&a);
	ASSERT_TRUE(std::filesystem::exists(swp));
	// Compaction works: the journal holds about one checkpoint, not fifty.
	ASSERT_TRUE(std::filesystem::file_size(swp) < 40000);
	a.SetSwapRecorder(nullptr);
	sm.Detach(&a, true);
	ASSERT_TRUE(!std::filesystem::exists(swp));
}


// Indenting a region leaves empty lines alone: with the whole buffer
// selected, the empty row after the final newline became a tab-only line.
TEST(Audit_IndentRegion_SkipsEmptyLines)
{
	TestHarness h;
	h.Buf().insert_text(0, 0, "a\n\nb\n");
	h.Buf().SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::MarkAllAndJumpEnd));
	ASSERT_TRUE(h.Exec(CommandId::IndentRegion));
	ASSERT_EQ(h.Text(), std::string("\ta\n\n\tb\n"));
	ASSERT_TRUE(h.Undo());
	ASSERT_EQ(h.Text(), std::string("a\n\nb\n"));
}


// Reflow never wraps before a line's first word: a first word longer than
// the width produced a prefix-only line (another one on each reflow).
TEST(Audit_Reflow_LongFirstWordNoBlankLine)
{
	const std::string url = "https://example.com/" + std::string(70, 'x');
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "see:\n\n" + url + " is the link\n");
		h.Buf().SetCursor(0, 2);
		ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph));
		ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph));
		ASSERT_EQ(h.Text(), std::string("see:\n\n" + url + "\nis the link\n"));
	}
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "- " + url + " x\n");
		h.Buf().SetCursor(0, 0);
		ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph));
		ASSERT_EQ(h.Text(), std::string("- " + url + "\n  x\n"));
	}
}


// A whitespace-only line separates paragraphs, and CRLF lines keep their
// CR at the end of each reflowed line.
TEST(Audit_Reflow_BlankishSeparatorAndCrlf)
{
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "First paragraph.\n  \nSecond paragraph.\n");
		h.Buf().SetCursor(0, 0);
		ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph));
		ASSERT_EQ(h.Text(), std::string("First paragraph.\n  \nSecond paragraph.\n"));
	}
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "one two\r\nthree four\r\nfive\r\n\r\nnext\r\n");
		h.Buf().SetCursor(0, 0);
		ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph));
		ASSERT_EQ(h.Text(), std::string("one two three four five\r\n\r\nnext\r\n"));
		ASSERT_TRUE(h.Undo());
		ASSERT_EQ(h.Text(), std::string("one two\r\nthree four\r\nfive\r\n\r\nnext\r\n"));
	}
	{
		// CRLF file without a final line ending: the last line has no CR.
		TestHarness h;
		h.Buf().insert_text(0, 0, "aaa bbb\r\nccc ddd\r\neee");
		h.Buf().SetCursor(0, 0);
		ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph));
		ASSERT_EQ(h.Text(), std::string("aaa bbb ccc ddd eee"));
	}
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "aaa bbb\r\nccc ddd\r\neee");
		h.Buf().SetCursor(0, 0);
		ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph, "", 8));
		ASSERT_EQ(h.Text(), std::string("aaa bbb\r\nccc ddd\r\neee"));
	}
}


// Reflow does not wrap right before a word that would then read as a list
// marker ("-", "1."), which turned the paragraph into a list next time.
TEST(Audit_Reflow_NoWrapBeforeMarkerWord)
{
	TestHarness h;
	h.Buf().insert_text(0, 0, "alpha beta - gamma delta\n");
	h.Buf().SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph, "", 11));
	const std::string once = h.Text();
	ASSERT_EQ(once, std::string("alpha beta -\ngamma delta\n"));
	h.Buf().SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::ReflowParagraph, "", 11));
	ASSERT_EQ(h.Text(), once);
}


// A counted backspace or delete that joins lines undoes in one step.
TEST(Audit_CountedDelete_AcrossJoin_OneUndo)
{
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "abc\nde");
		h.Buf().SetCursor(1, 1);
		ASSERT_TRUE(h.Exec(CommandId::Backspace, "", 3));
		ASSERT_EQ(h.Text(), std::string("abe"));
		ASSERT_TRUE(h.Undo());
		ASSERT_EQ(h.Text(), std::string("abc\nde"));
	}
	{
		TestHarness h;
		h.Buf().insert_text(0, 0, "abc\nde");
		h.Buf().SetCursor(2, 0);
		ASSERT_TRUE(h.Exec(CommandId::DeleteChar, "", 3));
		ASSERT_EQ(h.Text(), std::string("abe"));
		ASSERT_TRUE(h.Undo());
		ASSERT_EQ(h.Text(), std::string("abc\nde"));
	}
}


// Plain search scans the materialized text in one pass; it must report the
// same matches as a per-line search, also after scattered edits (many
// pieces) and at line boundaries.
TEST(Audit_Search_WholeBufferScanMatchesPerLine)
{
	std::mt19937 rng(7);
	const char alphabet[] = {'a', 'b', '\n', 'a', 'b', ' ', '\r'};
	for (int iter = 0; iter < 40; ++iter) {
		TestHarness h;
		std::string text;
		const int n = 50 + static_cast<int>(rng() % 400);
		for (int i = 0; i < n; ++i)
			text.push_back(alphabet[rng() % sizeof(alphabet)]);
		h.Buf().insert_text(0, 0, text);
		// Scattered edits fragment the piece table.
		for (int e = 0; e < 20; ++e) {
			const std::size_t y = rng() % h.Buf().Nrows();
			h.Buf().insert_text(static_cast<int>(y), 0, (rng() % 2) ? "ab" : "b");
		}
		const std::string all = h.Text();
		const std::string q   = (iter % 3 == 0) ? "ab" : (iter % 3 == 1) ? "b a" : "a";
		// Per-line reference.
		std::vector<std::pair<std::size_t, std::size_t> > want;
		std::size_t y = 0, start = 0;
		while (true) {
			const std::size_t nl   = all.find('\n', start);
			const std::string line = all.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
			for (std::size_t p = line.find(q); p != std::string::npos; p = line.find(q, p + q.size()))
				want.emplace_back(y, p);
			if (nl == std::string::npos)
				break;
			start = nl + 1;
			++y;
		}
		h.Buf().SetCursor(0, 0);
		ASSERT_TRUE(h.Exec(CommandId::FindStart));
		ASSERT_TRUE(h.Exec(CommandId::InsertText, q));
		if (want.empty()) {
			ASSERT_TRUE(h.EditorRef().Status().find(" 1/") == std::string::npos);
		} else {
			ASSERT_TRUE(h.EditorRef().Status().find("1/" + std::to_string(want.size())) != std::string::npos);
			for (std::size_t i = 0; i < want.size() && i < 10; ++i) {
				ASSERT_EQ(h.Buf().Cury(), want[i].first);
				ASSERT_EQ(h.Buf().Curx(), want[i].second);
				ASSERT_TRUE(h.Exec(CommandId::MoveRight));
			}
		}
		ASSERT_TRUE(h.Exec(CommandId::Refresh));
	}
}


// Indent/unindent and visual-line edits over many rows are one edit, not
// one per row (each costing time linear in the buffer): indenting 100k
// rows took minutes. Generous bound; typically well under a second.
TEST(Audit_RowRangeEdits_Scale)
{
	TestHarness h;
	std::string text;
	for (int i = 0; i < 100000; ++i)
		text += "line " + std::to_string(i) + "\n";
	h.Buf().insert_text(0, 0, text);
	h.Buf().SetCursor(0, 0);
	const auto t0 = std::chrono::steady_clock::now();
	ASSERT_TRUE(h.Exec(CommandId::MarkAllAndJumpEnd));
	ASSERT_TRUE(h.Exec(CommandId::IndentRegion));
	ASSERT_EQ(h.Buf().GetLineString(99999), std::string("\tline 99999"));
	h.Buf().SetCursor(0, 0);
	ASSERT_TRUE(h.Exec(CommandId::VisualLineModeToggle));
	for (int i = 0; i < 50000; ++i)
		(void) Execute(h.EditorRef(), CommandId::MoveDown);
	ASSERT_TRUE(h.Exec(CommandId::InsertText, "x"));
	ASSERT_EQ(h.Buf().GetLineString(49999), std::string("x\tline 49999"));
	ASSERT_EQ(h.Buf().GetLineString(50001), std::string("\tline 50001"));
	ASSERT_TRUE(h.Undo());
	ASSERT_TRUE(h.Undo());
	const auto secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	ASSERT_EQ(h.Text(), text);
	ASSERT_TRUE(secs < 20.0);
}


// Large undo texts are held as references to the buffer's storage: a
// replace-all over a large buffer keeps almost nothing in history, and undo,
// redo and the journal still reproduce the text.
TEST(Undo_LargeTextsHeldAsSpans)
{
	TempDir d("undo_spans");
	const std::string path = (d.path / "big.txt").string();
	std::string text;
	for (int i = 0; i < 20000; ++i)
		text += "alpha " + std::to_string(i) + " beta\n";
	{
		std::ofstream o(path, std::ios::binary);
		o << text;
	}
	std::remove(kte::SwapManager::ComputeSwapPathForFilename(path).c_str());
	TestHarness h;
	std::string err;
	ASSERT_TRUE(h.EditorRef().OpenFile(path, err));
	Buffer &b = h.Buf();
	regex_replace_all(h, "alpha", "gamma");
	std::string after = text;
	for (std::size_t p = after.find("alpha"); p != std::string::npos; p = after.find("alpha", p + 5))
		after.replace(p, 5, "gamma");
	ASSERT_EQ(h.Text(), after);
	// Two nodes (delete + insert) of ~400 KB each, held as spans.
	ASSERT_TRUE(b.Undo()->HistoryBytes() < 16384);
	const UndoNode *ins = b.Undo()->TreeForTests().current;
	ASSERT_TRUE(ins && ins->HasSpans());
	ASSERT_TRUE(ins->parent && ins->parent->HasSpans());

	// Kill a large region (a Delete node held as spans) and yank it back.
	b.SetCursor(0, 100);
	ASSERT_TRUE(h.Exec(CommandId::ToggleMark));
	b.SetCursor(0, 5000);
	ASSERT_TRUE(h.Exec(CommandId::KillRegion));
	const std::string killed = h.Text();
	ASSERT_TRUE(b.Undo()->HistoryBytes() < 32768);

	for (int i = 0; i < 3; ++i) {
		ASSERT_TRUE(h.Undo());
		ASSERT_TRUE(h.Undo());
		ASSERT_EQ(h.Text(), text);
		ASSERT_TRUE(h.Redo());
		ASSERT_EQ(h.Text(), after);
		ASSERT_TRUE(h.Redo());
		ASSERT_EQ(h.Text(), killed);
	}
	// The journal (fed from spans on undo/redo) replays to the same text.
	h.EditorRef().Swap()->Flush(&b);
	const std::string copy = (d.path / "crash.swp").string();
	{
		std::ofstream o(copy, std::ios::binary | std::ios::trunc);
		o << slurp(kte::SwapManager::ComputeSwapPathForTests(b));
	}
	Buffer b2;
	ASSERT_TRUE(b2.OpenFromFile(path, err));
	ASSERT_TRUE(kte::SwapManager::ReplayFile(b2, copy, err));
	ASSERT_EQ(b2.BytesForTests(), killed);
	b.SetDirty(false);
}


// Undo history has a byte budget: over it, branches off the current path and
// then the oldest edits go. History never lands on a state that did not
// exist, and once the saved state is gone the buffer stays modified.
TEST(Undo_ByteBudgetPrunesOldestAndBranches)
{
	TestHarness h;
	Buffer &b     = h.Buf();
	UndoSystem *u = b.Undo();
	u->SetByteBudget(64 * 1024);
	b.insert_text(0, 0, "base\n");
	u->mark_saved();
	b.SetDirty(false);

	// A branch: an edit, undone, then a different edit.
	ASSERT_TRUE(h.Exec(CommandId::InsertText, std::string(2000, 'b')));
	ASSERT_TRUE(h.Exec(CommandId::MoveLeft));
	ASSERT_TRUE(h.Undo());
	std::set<std::string> seen{h.Text()};
	// Many distinct edits, each its own node with ~1 KB of text.
	for (int i = 0; i < 400; ++i) {
		ASSERT_TRUE(h.Exec(CommandId::InsertText, std::string(1000, static_cast<char>('a' + i % 26))));
		seen.insert(h.Text());
		ASSERT_TRUE(h.Exec(CommandId::Newline));
		seen.insert(h.Text());
		ASSERT_TRUE(u->HistoryBytes() <= 64 * 1024);
	}
	const std::string tip = h.Text();
	// Undo everything that is left: the oldest edits (and the branch) are
	// gone, so this does not reach the saved text, and the buffer knows it.
	for (int i = 0; i < 2000; ++i)
		(void) Execute(h.EditorRef(), CommandId::Undo);
	ASSERT_TRUE(h.Text() != std::string("base\n"));
	ASSERT_TRUE(seen.count(h.Text()) == 1);
	ASSERT_TRUE(b.Dirty());
	for (int i = 0; i < 2000; ++i)
		(void) Execute(h.EditorRef(), CommandId::Redo);
	ASSERT_EQ(h.Text(), tip);
	ASSERT_TRUE(b.Dirty());
	u->mark_saved();
	ASSERT_TRUE(!b.Dirty());
}


// Pruning drops an undo group whole.
TEST(Undo_ByteBudgetKeepsGroupsWhole)
{
	// The replace's delete node (~1.3 KB) is large next to the gap between
	// budget and prune target, its insert node small: across filler sizes,
	// pruning would stop between the two unless groups are kept whole.
	std::string base;
	for (int i = 0; i < 300; ++i)
		base += "one ";
	base += "\n";
	for (int fill = 40; fill < 400; fill += 9) {
		TestHarness h;
		Buffer &b = h.Buf();
		b.Undo()->SetByteBudget(4096);
		b.insert_text(0, 0, base);
		std::set<std::string> seen{h.Text()};
		regex_replace_all(h, "one ", "x"); // a group: delete + insert
		seen.insert(h.Text());
		for (int i = 0; i < 12; ++i) {
			ASSERT_TRUE(h.Exec(CommandId::InsertText, std::string(static_cast<std::size_t>(fill), 'z')));
			seen.insert(h.Text());
			ASSERT_TRUE(h.Exec(CommandId::Newline));
			seen.insert(h.Text());
		}
		// Undo everything left, checking every state on the way.
		for (int i = 0; i < 100; ++i) {
			(void) Execute(h.EditorRef(), CommandId::Undo);
			ASSERT_TRUE(seen.count(h.Text()) == 1);
		}
	}
}
