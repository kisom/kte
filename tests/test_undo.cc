#include "Test.h"
#include "Buffer.h"


TEST (Undo_InsertRun_Coalesces)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Simulate two separate "typed" insert commands without committing in between.
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
	ASSERT_EQ(b.Rows().size(), (std::size_t) 1);
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("hi"));

	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string(""));
}


TEST (Undo_BackspaceRun_Coalesces)
{
	Buffer b;
	UndoSystem *u = b.Undo();
	ASSERT_TRUE(u != nullptr);

	// Seed content.
	b.insert_text(0, 0, std::string_view("abc"));
	b.SetCursor(3, 0);
	u->mark_saved();

	// Simulate two backspaces: delete 'c' then 'b'.
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

	// One undo should restore both characters.
	u->undo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("abc"));
}


TEST (Undo_Linear_RedoDiscardedAfterNewEdit)
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

	// New edit after undo should discard redo.
	u->Begin(UndoType::Insert);
	b.insert_text(0, 1, std::string_view("c"));
	u->Append('c');
	b.SetCursor(2, 0);
	u->commit();

	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("ac"));
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


TEST (Undo_NewEditFromPreFirstEdit_DiscardsOldHistory)
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

	// Old history should be gone: redo should not resurrect "ab".
	u->redo();
	ASSERT_EQ(std::string(b.Rows()[0]), std::string("x"));
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