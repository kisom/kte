/*
 * test_audit_regressions.cc - Regression tests for defects recorded in
 * docs/audits/claude-findings-20260923.md. Each test names its finding ID.
 */
#include "Test.h"

#include "tests/TestHarness.h"

#include "ErrorRecovery.h"
#include "PieceTable.h"
#include "UndoTree.h"

#include <chrono>
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


// D9: clearing the dirty flag (undo to the saved state) changes the version
// so version-keyed highlight caches are refreshed.
TEST(Audit_D9_SetDirtyFalse_BumpsVersion)
{
	Buffer b;
	b.SetDirty(true);
	const auto v = b.Version();
	b.SetDirty(false);
	ASSERT_TRUE(b.Version() != v);
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
