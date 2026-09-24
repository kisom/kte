/*
 * test_audit_regressions.cc - Regression tests for defects recorded in
 * docs/audits/claude-findings-20260923.md. Each test names its finding ID.
 */
#include "Test.h"

#include "tests/TestHarness.h"

#include "ErrorRecovery.h"
#include "PieceTable.h"
#include "RegexGuard.h"
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
