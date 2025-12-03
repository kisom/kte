#include "UndoSystem.h"
#include "Buffer.h"
#include <cassert>
#include <cstdio>


UndoSystem::UndoSystem(Buffer &owner, UndoTree &tree)
	: buf_(&owner), tree_(tree) {}


void
UndoSystem::Begin(UndoType type)
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
	(void) type;
}


void
UndoSystem::Append(char ch)
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
	(void) ch;
}


void
UndoSystem::Append(std::string_view text)
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
	(void) text;
}


void
UndoSystem::commit()
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
}


void
UndoSystem::undo()
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
}


void
UndoSystem::redo()
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
}


void
UndoSystem::mark_saved()
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
}


void
UndoSystem::discard_pending()
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
}


void
UndoSystem::clear()
{
	// STUB: Undo system incomplete - disabled until it can be properly implemented
}


void
UndoSystem::apply(const UndoNode *node, int direction)
{
	if (!node)
		return;
	switch (node->type) {
	case UndoType::Insert:
	case UndoType::Paste:
		if (direction > 0) {
			buf_->insert_text(node->row, node->col, node->text);
		} else {
			buf_->delete_text(node->row, node->col, node->text.size());
		}
		break;
	case UndoType::Delete:
		if (direction > 0) {
			buf_->delete_text(node->row, node->col, node->text.size());
		} else {
			buf_->insert_text(node->row, node->col, node->text);
		}
		break;
	case UndoType::Newline:
		if (direction > 0) {
			buf_->split_line(node->row, node->col);
		} else {
			buf_->join_lines(node->row);
		}
		break;
	case UndoType::DeleteRow:
		if (direction > 0) {
			buf_->delete_row(node->row);
		} else {
			buf_->insert_row(node->row, node->text);
		}
		break;
	}
}


void
UndoSystem::free_node(UndoNode *node)
{
	if (!node)
		return;
	// Free child subtree(s) and sibling branches
	if (node->child) {
		// Free entire redo list starting at child, including each subtree
		UndoNode *branch = node->child;
		while (branch) {
			UndoNode *next = branch->next;
			free_node(branch);
			branch = next;
		}
		node->child = nullptr;
	}
	delete node;
}


void
UndoSystem::free_branch(UndoNode *node)
{
	// Free a branch list (node and its next siblings) including their subtrees
	while (node) {
		UndoNode *next = node->next;
		free_node(node);
		node = next;
	}
}


static bool
dfs_find_parent(UndoNode *cur, UndoNode *target, UndoNode *&out_parent)
{
	if (!cur)
		return false;
	for (UndoNode *child = cur->child; child != nullptr; child = child->next) {
		if (child == target) {
			out_parent = cur;
			return true;
		}
		if (dfs_find_parent(child, target, out_parent))
			return true;
	}
	return false;
}


UndoNode *
UndoSystem::find_parent(UndoNode *from, UndoNode *target)
{
	if (!from || !target)
		return nullptr;
	if (from == target)
		return nullptr;
	UndoNode *parent = nullptr;
	dfs_find_parent(from, target, parent);
	return parent;
}


void
UndoSystem::update_dirty_flag()
{
	// dirty if current != saved
	bool dirty = (tree_.current != tree_.saved);
	buf_->SetDirty(dirty);
}


void
UndoSystem::UpdateBufferReference(Buffer &new_buf)
{
	buf_ = &new_buf;
}


// ---- Debug helpers ----
const char *
UndoSystem::type_str(UndoType t)
{
	switch (t) {
	case UndoType::Insert:
		return "Insert";
	case UndoType::Delete:
		return "Delete";
	case UndoType::Paste:
		return "Paste";
	case UndoType::Newline:
		return "Newline";
	case UndoType::DeleteRow:
		return "DeleteRow";
	}
	return "?";
}


bool
UndoSystem::is_descendant(UndoNode *root, const UndoNode *target)
{
	if (!root || !target)
		return false;
	if (root == target)
		return true;
	for (UndoNode *child = root->child; child != nullptr; child = child->next) {
		if (is_descendant(child, target))
			return true;
	}
	return false;
}


void
UndoSystem::debug_log(const char *op) const
{
#ifdef KTE_UNDO_DEBUG
	int row           = static_cast<int>(buf_->Cury());
	int col           = static_cast<int>(buf_->Curx());
	const UndoNode *p = tree_.pending;
	std::fprintf(stderr,
	             "[UNDO] %s cur=(%d,%d) pending=%p t=%s r=%d c=%d nlen=%zu current=%p saved=%p\n",
	             op,
	             row, col,
	             (const void *) p,
	             p ? type_str(p->type) : "-",
	             p ? p->row : -1,
	             p ? p->col : -1,
	             p ? p->text.size() : 0,
	             (void *) tree_.current,
	             (void *) tree_.saved);
#else
	(void) op;
#endif
}