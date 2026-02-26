#include "Test.h"
#include "Buffer.h"

#include "Command.h"
#include "Editor.h"

#include <cstddef>
#include <random>

#if defined(KTE_TESTS)
#include <unordered_set>


static void
validate_undo_subtree(const UndoNode *node, const UndoNode *expected_parent,
                      std::unordered_set<const UndoNode *> &seen)
{
	ASSERT_TRUE(node != nullptr);
	ASSERT_TRUE(seen.find(node) == seen.end());
	seen.insert(node);
	ASSERT_TRUE(node->parent == expected_parent);

	// Validate each redo branch under this node.
	for (const UndoNode *ch = node->child; ch != nullptr; ch = ch->next) {
		validate_undo_subtree(ch, node, seen);
	}
}


static void
validate_undo_tree(const UndoSystem &u)
{
	const UndoTree &t = u.TreeForTests();

	std::unordered_set<const UndoNode *> seen;
	for (const UndoNode *root = t.root; root != nullptr; root = root->next) {
		validate_undo_subtree(root, nullptr, seen);
	}

	// current/saved must either be null or be reachable from some root.
	if (t.current)
		ASSERT_TRUE(seen.find(t.current) != seen.end());
	if (t.saved)
		ASSERT_TRUE(seen.find(t.saved) != seen.end());

	// pending is detached (not part of the committed tree).
	if (t.pending) {
		ASSERT_TRUE(seen.find(t.pending) == seen.end());
		ASSERT_TRUE(t.pending->parent == nullptr);
		ASSERT_TRUE(t.pending->child == nullptr);
		ASSERT_TRUE(t.pending->next == nullptr);
	}
}
#endif


// The undo suite aims to cover invariants with a small, adversarial test matrix.


TEST (Undo_InsertRun_Coalesces_OneStep)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("h"));
	u->Append('h');
	b.SetCursor(1, 0);

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("i"));
	u->Append('i');
	b.SetCursor(2, 0);
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("hi"));
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));
}


TEST (Undo_InsertRun_BreaksOnNonAdjacentCursor)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);

	// Jump the cursor; next insert should not coalesce.
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("b"));
	u->Append('b');
	b.SetCursor(1, 0);
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ba"));
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));
}


TEST (Undo_BackspaceRun_Coalesces_OneStep)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.insert_text(0, 0, std::string_view("abc"));
	b.SetCursor(3, 0);

	// Delete 'c' then 'b' with backspace shape.
	{
		const auto &rows = b.Rows();
		char deleted     = rows[0][2];
		b.delete_text(0, 2, 1);
		b.SetCursor(2, 0);
		u->Begin(UndoType::Delete);
		u->Append(deleted);
	}
	{
		const auto &rows = b.Rows();
		char deleted     = rows[0][1];
		b.delete_text(0, 1, 1);
		b.SetCursor(1, 0);
		u->Begin(UndoType::Delete);
		u->Append(deleted);
	}
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("abc"));
}


TEST (Undo_DeleteKeyRun_Coalesces_OneStep)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.insert_text(0, 0, std::string_view("abcd"));
	// Simulate delete-key at col 1 twice (cursor stays).
	b.SetCursor(1, 0);
	{
		const auto &rows = b.Rows();
		char deleted     = rows[0][1];
		b.delete_text(0, 1, 1);
		b.SetCursor(1, 0);
		u->Begin(UndoType::Delete);
		u->Append(deleted);
	}
	{
		const auto &rows = b.Rows();
		char deleted     = rows[0][1];
		b.delete_text(0, 1, 1);
		b.SetCursor(1, 0);
		u->Begin(UndoType::Delete);
		u->Append(deleted);
	}
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ad"));

	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("abcd"));
}


TEST (Undo_Newline_IsStandalone)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Seed with content and split in the middle (not at EOF) so (row=1,col=0)
	// is always addressable and cannot be clamped in unexpected ways.
	b.insert_text(0, 0, std::string_view("hi"));
	b.SetCursor(1, 0);
	const std::string before_nl = b.BytesForTests();
	// Newline should always be its own undo step.
	u->Begin(UndoType::Newline);
	b.split_line(0, 1);
	u->commit();
	const std::string after_nl = b.BytesForTests();

	// Move cursor to insertion site so `UndoSystem::Begin()` captures correct (row,col).
	b.SetCursor(0, 1);
	u->Begin(UndoType::Insert);
	b.insert_text(1, 0, std::string_view("x"));
	u->Append('x');
	b.SetCursor(1, 1);
	u->commit();

	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("xi"));
	u->undo();
	// Undoing the insert should not also undo the newline.
	ASSERT_EQ(b.BytesForTests(), after_nl);
	u->undo();
	ASSERT_EQ(b.BytesForTests(), before_nl);
}


TEST (Undo_ExplicitGroup_UndoesAsUnit)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.SetCursor(0, 0);
	(void) u->BeginGroup();
	// Simulate two separate committed edits inside a group.
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();
	u->EndGroup();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));
}


TEST (Undo_Branching_RedoBranchSelectionDeterministic)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// A then B then C
	b.SetCursor(0, 0);
	for (char ch: std::string("ABC")) {
		u->Begin(UndoType::Insert);
		b.insert_text(0, b.Curx(), std::string_view(&ch, 1));
		u->Append(ch);
		b.SetCursor(b.Curx() + 1, 0);
		u->commit();
	}
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ABC"));

	// Undo twice -> back to "A"
	u->undo();
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("A"));

	// Type D to create a new branch.
	u->Begin(UndoType::Insert);
	char d = 'D';
	b.insert_text(0, 1, std::string_view(&d, 1));
	u->Append('D');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("AD"));

	// Undo D, then redo branch 0 should redo D (new head).
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("A"));
	u->redo(0);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("AD"));

	// Undo back to A again, redo branch 1 should follow the older path (to AB).
	u->undo();
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("AB"));
}


TEST (Undo_DirtyFlag_CrossesMarkSaved)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("x"));
	u->Append('x');
	b.SetCursor(1, 0);
	u->commit();
	if (auto *u2 = b.Undo())
		u2->mark_saved();
	b.SetDirty(false);
	ASSERT_TRUE(!b.Dirty());

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("y"));
	u->Append('y');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_TRUE(b.Dirty());

	u->undo();
	ASSERT_TRUE(!b.Dirty());
}


TEST (Undo_RoundTrip_Lossless_RandomEdits)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	std::mt19937 rng(123);
	std::uniform_int_distribution<int> pick(0, 1);
	std::uniform_int_distribution<int> ch('a', 'z');

	// Build a short random sequence of inserts and deletes.
	for (int i = 0; i < 200; ++i) {
		const std::string cur = b.AsString();
		const bool do_insert  = (cur.empty() || pick(rng) == 0);
		if (do_insert) {
			char c = static_cast<char>(ch(rng));
			u->Begin(UndoType::Insert);
			b.insert_text(0, b.Curx(), std::string_view(&c, 1));
			u->Append(c);
			b.SetCursor(b.Curx() + 1, 0);
			u->commit();
		} else {
			// Delete one char at a stable position.
			std::size_t x = b.Curx();
			if (x >= b.Rows()[0].size())
				x = b.Rows()[0].size() - 1;
			char deleted = b.Rows()[0][x];
			b.delete_text(0, static_cast<int>(x), 1);
			b.SetCursor(x, 0);
			u->Begin(UndoType::Delete);
			u->Append(deleted);
			u->commit();
		}
	}

	const std::string final = b.AsString();
	// Undo back to start.
	for (int i = 0; i < 1000; ++i) {
		std::string before = b.AsString();
		u->undo();
		if (b.AsString() == before)
			break;
	}
	// Redo forward; should end at exact final bytes.
	for (int i = 0; i < 1000; ++i) {
		std::string before = b.AsString();
		u->redo(0);
		if (b.AsString() == before)
			break;
	}
	ASSERT_EQ(b.AsString(), final);
}


// Legacy/extended undo tests follow. Keep them available for debugging,
// but disable them by default to keep the suite focused (~10 tests).
#if 1


TEST (Undo_Branching_RedoPreservedAfterNewEdit)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// New edit after undo creates a new branch; the old redo should remain as an alternate branch.
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("c"));
	u->Append('c');
	b.SetCursor(2, 0);
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));

	// No further redo from the tip.
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));

	// Undo back to the branch point and redo the original branch.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
}


TEST (Undo_DirtyFlag_MarkSavedAndUndoRedo)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	u->mark_saved();
	ASSERT_TRUE(!b.Dirty());

	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("x"));
	u->Append('x');
	b.SetCursor(1, 0);
	u->commit();

	ASSERT_TRUE(b.Dirty());
	u->undo();
	ASSERT_TRUE(!b.Dirty());
	u->redo();
	ASSERT_TRUE(b.Dirty());
}


TEST (Undo_Newline_UndoRedo_SplitJoin)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Seed a single line and split it.
	b.insert_text(0, 0, std::string_view("hello"));
	b.SetCursor(2, 0); // split after "he"
	u->Begin(UndoType::Newline);
	b.split_line(0, 2);
	u->commit();

	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("he"));
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("llo"));

	// Undo should join the lines back.
	u->undo();
	ASSERT_EQ(b.Rows().size(), (std::size_t) 1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("hello"));

	// Redo should split again at the same point.
	u->redo();
	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("he"));
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("llo"));
}


TEST (Undo_DeleteKeyRun_Coalesces)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Seed content: delete-key semantics keep cursor at the same column.
	b.insert_text(0, 0, std::string_view("abcd"));
	b.SetCursor(1, 0); // on 'b'

	// Delete 'b'
	{
		const auto &rows = b.Rows();
		char deleted     = rows[0][1];
		u->Begin(UndoType::Delete);
		b.delete_text(0, 1, 1);
		u->Append(deleted);
		b.SetCursor(1, 0);
	}
	// Delete next char (was 'c', now at same col=1)
	{
		const auto &rows = b.Rows();
		char deleted     = rows[0][1];
		u->Begin(UndoType::Delete);
		b.delete_text(0, 1, 1);
		u->Append(deleted);
		b.SetCursor(1, 0);
	}

	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ad"));

	// One undo should restore both deleted characters.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("abcd"));
}


TEST (Undo_UndoPastFirstEdit_RedoFromPreFirstEdit)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Commit two separate insert edits.
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();

	b.SetCursor(1, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	// Undo twice: we should reach the pre-first-edit state.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));

	// Redo twice should restore both edits.
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
}


TEST (Undo_NewEditFromPreFirstEdit_PreservesOldHistoryAsAlternateRootBranch)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Build up two edits.
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();

	b.SetCursor(1, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	// Undo past first edit so current becomes null.
	u->undo();
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));

	// Commit a new edit from the pre-first-edit state.
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("x"));
	u->Append('x');
	b.SetCursor(1, 0);
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("x"));

	// From the tip, no further redo.
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("x"));

	// Undo back to pre-first-edit and select the older root branch.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
}


TEST (Undo_MultiLineDelete_ConsumesNewline_UndoRestores)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Create two lines. PieceTable treats '\n' between logical lines.
	b.insert_text(0, 0, std::string_view("ab\ncd"));
	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("cd"));

	// Delete spanning the newline: delete "b\n" starting at (0,1).
	b.SetCursor(1, 0);
	u->Begin(UndoType::Delete);
	b.delete_text(0, 1, 2);
	u->Append(std::string_view("b\n"));
	u->commit();

	ASSERT_EQ(b.Rows().size(), (std::size_t) 1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("acd"));

	// Undo should restore exact original text/line structure.
	u->undo();
	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("cd"));
}


TEST (Undo_DeleteIndent_UndoRestoresCursorAtText)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Seed 3-line content with indentation on the middle line.
	b.insert_text(0, 0,
	              std::string_view("I did a thing\n  and then I edited a thing\nbut there were gaps"));
	ASSERT_EQ(b.Rows().size(), (std::size_t) 3);

	// Cursor at start of the line (before spaces), then C-d C-d deletes two spaces.
	b.SetCursor(0, 1);
	for (int i = 0; i < 2; ++i) {
		const auto &rows = b.Rows();
		char deleted     = rows[1][0];
		ASSERT_EQ(deleted, ' ');
		u->Begin(UndoType::Delete);
		b.delete_text(1, 0, 1);
		u->Append(deleted);
		b.SetCursor(0, 1); // delete-key keeps col the same
	}
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[1]), std::string("and then I edited a thing"));
	ASSERT_EQ(b.Cury(), (std::size_t) 1);
	ASSERT_EQ(b.Curx(), (std::size_t) 0);

	// Undo should restore indentation, and keep cursor on the text (at 'a'), not at EOL.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("  and then I edited a thing"));
	ASSERT_EQ(b.Cury(), (std::size_t) 1);
	ASSERT_EQ(b.Curx(), (std::size_t) 2);
}


TEST (Undo_StructuralInvariants_BranchingAndRoots)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Build history: a -> b
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();

	b.SetCursor(1, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	// Undo past first edit; now create a new root-level branch x.
	u->undo();
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));

	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("x"));
	u->Append('x');
	b.SetCursor(1, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("x"));

	// Return to the older root branch.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Create a normal branch under 'a'.
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("c"));
	u->Append('c');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));

	validate_undo_tree(*u);
}


TEST (Undo_BranchSelection_ThreeSiblingsAndHeadPersists)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Root: a
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Branch 1: a->b
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	// Back to branch point.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Branch 2: a->c
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("c"));
	u->Append('c');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));

	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Branch 3: a->d
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("d"));
	u->Append('d');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ad"));

	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Under 'a', the sibling list should now contain 3 branches.
	validate_undo_tree(*u);

	// Select the 3rd sibling (branch_index=2) which should be the oldest ("b"), and make it active.
	u->redo(2);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Since we selected "b", redo with default should now follow "b" again.
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Select another branch by index and ensure it becomes the new default.
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ad"));
	u->undo();
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ad"));
	u->undo();

	// Out-of-range selection should be a no-op.
	u->redo(99);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	validate_undo_tree(*u);
}

#endif


// Additional legacy tests below are useful, but kept disabled by default.
#if 1

TEST (Undo_Branching_SwitchBetweenTwoRedoBranches_TextAndCursor)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Build A->B.
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	ASSERT_EQ(b.Cury(), (std::size_t) 0);
	ASSERT_EQ(b.Curx(), (std::size_t) 1);

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
	ASSERT_EQ(b.Curx(), (std::size_t) 2);

	// Undo to A.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	ASSERT_EQ(b.Curx(), (std::size_t) 1);

	// Create sibling branch A->C.
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("c"));
	u->Append('c');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));
	ASSERT_EQ(b.Curx(), (std::size_t) 2);

	// Back to A.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	ASSERT_EQ(b.Curx(), (std::size_t) 1);

	// Redo into B as the alternate branch (older sibling), and confirm cursor is consistent.
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
	ASSERT_EQ(b.Curx(), (std::size_t) 2);

	// Both branches remain reachable: undo to A, redo defaults to B (head reordered).
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	// And the other branch C should still be selectable.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));
	ASSERT_EQ(b.Curx(), (std::size_t) 2);

	// After selecting C, default redo from A should now follow C.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));

	validate_undo_tree(*u);
}


TEST (Undo_Randomized_Deterministic_EditUndoRedoBranchSelect)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	std::mt19937 rng(0xC0FFEEu);
	std::uniform_int_distribution<int> op(0, 99);
	std::uniform_int_distribution<int> ch(0, 25);

	const int steps      = 300;
	const int max_len    = 40;
	const int max_branch = 4;

	for (int i = 0; i < steps; ++i) {
		ASSERT_TRUE(!b.Rows().empty());
		ASSERT_EQ(b.Cury(), (std::size_t) 0);
		ASSERT_EQ(b.Rows().size(), (std::size_t) 1);
		ASSERT_TRUE(b.Curx() <= b.Rows()[0].size());

		validate_undo_tree(*u);

		int r           = op(rng);
		std::string cur = std::string(b.Rows()[0]);
		int len         = static_cast<int>(cur.size());

		if (r < 40 && len < max_len) {
			// Insert one char at end as a standalone committed node.
			char c = static_cast<char>('a' + ch(rng));
			b.SetCursor(static_cast<std::size_t>(len), 0);
			u->Begin(UndoType::Insert);
			b.insert_text(0, len, std::string_view(&c, 1));
			u->Append(c);
			b.SetCursor(static_cast<std::size_t>(len + 1), 0);
			u->commit();
		} else if (r < 60 && len > 0) {
			// Backspace at end as a standalone committed node.
			char deleted = cur[static_cast<std::size_t>(len - 1)];
			b.delete_text(0, len - 1, 1);
			b.SetCursor(static_cast<std::size_t>(len - 1), 0);
			u->Begin(UndoType::Delete);
			u->Append(deleted);
			u->commit();
		} else if (r < 80) {
			// Undo then redo should round-trip to the exact same node/text/cursor when possible.
			const UndoNode *before_node = u->TreeForTests().current;
			const std::string before_text(std::string(b.Rows()[0]));
			const std::size_t before_x = b.Curx();

			if (before_node) {
				u->undo();
				u->redo();
				ASSERT_TRUE(u->TreeForTests().current == before_node);
				ASSERT_EQ(std::string(b.Rows()[0]), before_text);
				ASSERT_EQ(b.Curx(), before_x);
			} else {
				// Nothing to undo; just exercise redo/branch-select paths.
				u->redo();
			}
		} else if (r < 90) {
			u->undo();
		} else {
			int idx = static_cast<int>(rng() % static_cast<std::uint32_t>(max_branch));
			if ((rng() % 8u) == 0u)
				idx = 99; // intentionally out of range sometimes
			u->redo(idx);
		}
	}

	validate_undo_tree(*u);
}


TEST (Undo_PendingCoalescedRun_UndoCommitsThenUndoes)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Create a coalesced insert run without an explicit commit.
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	// undo() should implicitly commit pending and then undo it as one step.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));

	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	validate_undo_tree(*u);
}


TEST (Undo_PendingRunAtBranchPoint_UndoThenBranchSelectionStillWorks)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Build a->b.
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	// Undo to the branch point.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Start a pending insert "c" at the branch point, but don't commit.
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("c"));
	u->Append('c');
	b.SetCursor(2, 0);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));

	// Undo should seal the pending "c" as a new branch, then undo it, leaving us at "a".
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// The active redo should now be "c".
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	// Select the older "b" branch.
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));

	validate_undo_tree(*u);
}


TEST (Undo_SavedNodeOnOtherBranch_DirtyClearsWhenReturning)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Build a->b and mark saved at the tip.
	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	b.SetCursor(1, 0);
	u->commit();

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	b.SetCursor(2, 0);
	u->commit();
	u->mark_saved();
	ASSERT_TRUE(!b.Dirty());

	// Move to a different branch.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("c"));
	u->Append('c');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));
	ASSERT_TRUE(b.Dirty());

	// Return to the saved node by selecting the older branch.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("a"));
	u->redo(1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ab"));
	ASSERT_TRUE(!b.Dirty());

	validate_undo_tree(*u);
}


TEST (Undo_Clear_AfterSaved_ResetsStateSafely)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 0, std::string_view("x"));
	u->Append('x');
	b.SetCursor(1, 0);
	u->commit();
	u->mark_saved();
	ASSERT_TRUE(!b.Dirty());

	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("y"));
	u->Append('y');
	b.SetCursor(2, 0);
	u->commit();
	ASSERT_TRUE(b.Dirty());

	u->clear();
	ASSERT_TRUE(!b.Dirty());
	// clear() resets undo history, but does not mutate buffer contents.
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("xy"));

	validate_undo_tree(*u);
}


TEST (Undo_Command_UndoHonorsRepeatCount)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	ed.AddBuffer(std::move(b));
	Buffer *buf = ed.CurrentBuffer();
	ASSERT_TRUE(buf != nullptr);
	UndoSystem *u = buf->Undo();
	ASSERT_TRUE(u != nullptr);

	// Create two committed steps using the undo system directly.
	buf->SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	buf->insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	buf->SetCursor(1, 0);
	u->commit();

	u->Begin(UndoType::Insert);
	buf->insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	buf->SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string("ab"));

	// Undo twice via command repeat count.
	ed.SetUniversalArg(1, 2);
	ASSERT_TRUE(Execute(ed, CommandId::Undo));
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string(""));

	validate_undo_tree(*u);
}


TEST (Undo_Command_RedoCountSelectsBranch)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	ed.AddBuffer(std::move(b));
	Buffer *buf = ed.CurrentBuffer();
	ASSERT_TRUE(buf != nullptr);
	UndoSystem *u = buf->Undo();
	ASSERT_TRUE(u != nullptr);

	// Build a->b.
	buf->SetCursor(0, 0);
	u->Begin(UndoType::Insert);
	buf->insert_text(0, 0, std::string_view("a"));
	u->Append('a');
	buf->SetCursor(1, 0);
	u->commit();

	u->Begin(UndoType::Insert);
	buf->insert_text(0, 1, std::string_view("b"));
	u->Append('b');
	buf->SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string("ab"));

	// Undo to the branch point and create a sibling branch "c".
	u->undo();
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string("a"));

	u->Begin(UndoType::Insert);
	buf->insert_text(0, 1, std::string_view("c"));
	u->Append('c');
	buf->SetCursor(2, 0);
	u->commit();
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string("ac"));

	// Back to branch point.
	ASSERT_TRUE(Execute(ed, CommandId::Undo));
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string("a"));

	// Command redo with count=2 should select branch_index=1 (the older "b" branch).
	ed.SetUniversalArg(1, 2);
	ASSERT_TRUE(Execute(ed, CommandId::Redo));
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string("ab"));

	// After selection, "b" should be the default redo from the branch point.
	ASSERT_TRUE(Execute(ed, CommandId::Undo));
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string("a"));
	ASSERT_TRUE(Execute(ed, CommandId::Redo));
	ASSERT_EQ(std::string(buf->Rows()[0]), std::string("ab"));

	validate_undo_tree(*u);
}


TEST (Undo_InsertRow_UndoDeletesRow)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Seed two lines so insert_row has proper newline context.
	b.insert_text(0, 0, std::string_view("first\nlast"));
	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);

	// Insert a row at position 1 (between first and last), then record it.
	b.insert_row(1, std::string_view("second"));
	ASSERT_EQ(b.Rows().size(), (std::size_t) 3);
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("second"));

	b.SetCursor(0, 1);
	u->Begin(UndoType::InsertRow);
	u->Append(std::string_view("second"));
	u->commit();

	// Undo should remove the inserted row.
	u->undo();
	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("first"));
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("last"));

	// Redo should re-insert it.
	u->redo();
	ASSERT_EQ(b.Rows().size(), (std::size_t) 3);
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("second"));

	validate_undo_tree(*u);
}


TEST (Undo_DeleteRow_UndoRestoresRow)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	b.insert_text(0, 0, std::string_view("alpha\nbeta\ngamma"));
	ASSERT_EQ(b.Rows().size(), (std::size_t) 3);

	// Record a DeleteRow for row 1 ("beta").
	b.SetCursor(0, 1);
	u->Begin(UndoType::DeleteRow);
	u->Append(static_cast<std::string>(b.Rows()[1]));
	u->commit();
	b.delete_row(1);

	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("alpha"));
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("gamma"));

	// Undo should restore "beta" at row 1.
	u->undo();
	ASSERT_EQ(b.Rows().size(), (std::size_t) 3);
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("beta"));

	// Redo should delete it again.
	u->redo();
	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("gamma"));

	validate_undo_tree(*u);
}


TEST (Undo_InsertRow_IsStandalone)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Seed with two lines so InsertRow has proper newline context.
	b.insert_text(0, 0, std::string_view("x\nend"));
	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);

	// Start a pending insert on row 0.
	b.SetCursor(1, 0);
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("y"));
	u->Append('y');
	b.SetCursor(2, 0);

	// InsertRow should seal the pending "y" and become its own step.
	b.insert_row(1, std::string_view("row2"));
	b.SetCursor(0, 1);
	u->Begin(UndoType::InsertRow);
	u->Append(std::string_view("row2"));
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("xy"));
	ASSERT_EQ(std::string(b.Rows()[1]), std::string("row2"));
	ASSERT_EQ(b.Rows().size(), (std::size_t) 3);

	// Undo InsertRow only.
	u->undo();
	ASSERT_EQ(b.Rows().size(), (std::size_t) 2);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("xy"));

	// Undo the insert "y".
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("x"));

	validate_undo_tree(*u);
}


TEST (Undo_GroupedDeleteAndInsertRows_UndoesAsUnit)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Seed three lines (with trailing newline so delete_row/insert_row work cleanly).
	b.insert_text(0, 0, std::string_view("aaa\nbbb\nccc\n"));
	ASSERT_EQ(b.Rows().size(), (std::size_t) 4); // 3 content + 1 empty trailing
	const std::string original = b.AsString();

	// Group: delete content rows then insert replacements (simulates reflow).
	(void) u->BeginGroup();

	// Delete rows 2,1,0 in reverse order (like reflow does).
	for (int i = 2; i >= 0; --i) {
		b.SetCursor(0, static_cast<std::size_t>(i));
		u->Begin(UndoType::DeleteRow);
		u->Append(static_cast<std::string>(b.Rows()[static_cast<std::size_t>(i)]));
		u->commit();
		b.delete_row(i);
	}

	// Insert replacement rows.
	b.insert_row(0, std::string_view("aaa bbb"));
	b.SetCursor(0, 0);
	u->Begin(UndoType::InsertRow);
	u->Append(std::string_view("aaa bbb"));
	u->commit();

	b.insert_row(1, std::string_view("ccc"));
	b.SetCursor(0, 1);
	u->Begin(UndoType::InsertRow);
	u->Append(std::string_view("ccc"));
	u->commit();

	u->EndGroup();

	const std::string reflowed = b.AsString();

	// Single undo should restore original content.
	u->undo();
	ASSERT_EQ(b.AsString(), original);

	// Redo should restore the reflowed state.
	u->redo();
	ASSERT_EQ(b.AsString(), reflowed);

	validate_undo_tree(*u);
}


#endif // legacy tests