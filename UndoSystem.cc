#include "UndoSystem.h"
#include "Buffer.h"
#include <cassert>
#include <cstdio>


UndoSystem::UndoSystem(Buffer &owner, UndoTree &tree)
	: buf_(&owner), tree_(tree) {}


void
UndoSystem::Begin(UndoType type)
{
	if (!buf_)
		return;
	const int row = static_cast<int>(buf_->Cury());
	const int col = static_cast<int>(buf_->Curx());

	// Some operations should always be standalone undo steps.
	const bool always_standalone = (type == UndoType::Newline || type == UndoType::DeleteRow);
	if (always_standalone) {
		commit();
	}

	if (tree_.pending) {
		if (tree_.pending->type == type) {
			// Typed-run coalescing rules.
			switch (type) {
			case UndoType::Insert:
			case UndoType::Paste: {
				// Cursor must be at the end of the pending insert.
				if (tree_.pending->row == row
				    && col == tree_.pending->col + static_cast<int>(tree_.pending->text.size())) {
					pending_mode_ = PendingAppendMode::Append;
					return;
				}
				break;
			}
			case UndoType::Delete: {
				if (tree_.pending->row == row) {
					// Two common delete shapes:
					// 1) backspace-run: cursor moves left each time (so new col is pending.col - 1)
					// 2) delete-run: cursor stays, always deleting at the same col
					if (col == tree_.pending->col) {
						pending_mode_ = PendingAppendMode::Append;
						return;
					}
					if (col + 1 == tree_.pending->col) {
						// Extend a backspace run to the left; update the start column now.
						tree_.pending->col = col;
						pending_mode_      = PendingAppendMode::Prepend;
						return;
					}
				}
				break;
			}
			case UndoType::Newline:
			case UndoType::DeleteRow:
				break;
			}
		}
		// Can't coalesce: seal the previous pending step.
		commit();
	}

	// Start a new pending node.
	tree_.pending       = new UndoNode{};
	tree_.pending->type = type;
	tree_.pending->row  = row;
	tree_.pending->col  = col;
	tree_.pending->text.clear();
	tree_.pending->child = nullptr;
	tree_.pending->next  = nullptr;
	pending_mode_        = PendingAppendMode::Append;
}


void
UndoSystem::Append(char ch)
{
	if (!tree_.pending)
		return;
	if (pending_mode_ == PendingAppendMode::Prepend) {
		tree_.pending->text.insert(tree_.pending->text.begin(), ch);
	} else {
		tree_.pending->text.push_back(ch);
	}
}


void
UndoSystem::Append(std::string_view text)
{
	if (!tree_.pending)
		return;
	if (text.empty())
		return;
	if (pending_mode_ == PendingAppendMode::Prepend) {
		tree_.pending->text.insert(0, text.data(), text.size());
	} else {
		tree_.pending->text.append(text.data(), text.size());
	}
}


void
UndoSystem::commit()
{
	if (!tree_.pending)
		return;

	// Drop empty text batches for text-based operations.
	if ((tree_.pending->type == UndoType::Insert || tree_.pending->type == UndoType::Delete
	     || tree_.pending->type == UndoType::Paste)
	    && tree_.pending->text.empty()) {
		delete tree_.pending;
		tree_.pending = nullptr;
		pending_mode_ = PendingAppendMode::Append;
		return;
	}

	// Linear semantics: if we are not at the tip, discard redo.
	if (tree_.current && tree_.current->child) {
		// Prevent dangling `saved` pointer if it sits in the discarded redo chain.
		if (tree_.saved && is_descendant(tree_.current->child, tree_.saved)) {
			tree_.saved = nullptr;
		}
		free_branch(tree_.current->child);
		tree_.current->child = nullptr;
	}

	if (!tree_.root) {
		tree_.root    = tree_.pending;
		tree_.current = tree_.pending;
	} else if (!tree_.current) {
		// We are at the "pre-first-edit" state. Attach as the new root child.
		// For v1 linear history, this means starting the chain anew.
		// The existing root represents edits from the past; attach the new node as the new root.
		// (This situation happens after undoing past the first node.)
		if (tree_.saved && is_descendant(tree_.root, tree_.saved)) {
			// ok
		}
		// Discard the old root chain because it is redo from the pre-edit state.
		if (tree_.saved && is_descendant(tree_.root, tree_.saved)) {
			tree_.saved = nullptr;
		}
		free_node(tree_.root);
		tree_.root    = tree_.pending;
		tree_.current = tree_.pending;
	} else {
		tree_.current->child = tree_.pending;
		tree_.current        = tree_.pending;
	}

	tree_.pending = nullptr;
	pending_mode_ = PendingAppendMode::Append;
	update_dirty_flag();
}


void
UndoSystem::undo()
{
	// Seal any in-progress typed run before undo.
	commit();
	if (!tree_.current)
		return;
	debug_log("undo");
	apply(tree_.current, -1);
	UndoNode *parent = find_parent(tree_.root, tree_.current);
	tree_.current    = parent;
	update_dirty_flag();
}


void
UndoSystem::redo()
{
	commit();
	UndoNode *next = nullptr;
	if (!tree_.current) {
		next = tree_.root;
	} else {
		next = tree_.current->child;
	}
	if (!next)
		return;
	debug_log("redo");
	apply(next, +1);
	tree_.current = next;
	update_dirty_flag();
}


void
UndoSystem::mark_saved()
{
	commit();
	tree_.saved = tree_.current;
	update_dirty_flag();
}


void
UndoSystem::discard_pending()
{
	if (tree_.pending) {
		delete tree_.pending;
		tree_.pending = nullptr;
	}
	pending_mode_ = PendingAppendMode::Append;
}


void
UndoSystem::clear()
{
	discard_pending();
	free_node(tree_.root);
	tree_.root    = nullptr;
	tree_.current = nullptr;
	tree_.saved   = nullptr;
	update_dirty_flag();
}


void
UndoSystem::apply(const UndoNode *node, int direction)
{
	if (!node)
		return;
	// Cursor positioning: keep the point at a sensible location after undo/redo.
	// Low-level Buffer edit primitives do not move the cursor.
	switch (node->type) {
	case UndoType::Insert:
	case UndoType::Paste:
		if (direction > 0) {
			buf_->insert_text(node->row, node->col, node->text);
			buf_->SetCursor(static_cast<std::size_t>(node->col + node->text.size()),
			                static_cast<std::size_t>(node->row));
		} else {
			buf_->delete_text(node->row, node->col, node->text.size());
			buf_->SetCursor(static_cast<std::size_t>(node->col), static_cast<std::size_t>(node->row));
		}
		break;
	case UndoType::Delete:
		if (direction > 0) {
			buf_->delete_text(node->row, node->col, node->text.size());
			buf_->SetCursor(static_cast<std::size_t>(node->col), static_cast<std::size_t>(node->row));
		} else {
			buf_->insert_text(node->row, node->col, node->text);
			buf_->SetCursor(static_cast<std::size_t>(node->col + node->text.size()),
			                static_cast<std::size_t>(node->row));
		}
		break;
	case UndoType::Newline:
		if (direction > 0) {
			buf_->split_line(node->row, node->col);
			buf_->SetCursor(0, static_cast<std::size_t>(node->row + 1));
		} else {
			buf_->join_lines(node->row);
			buf_->SetCursor(static_cast<std::size_t>(node->col), static_cast<std::size_t>(node->row));
		}
		break;
	case UndoType::DeleteRow:
		if (direction > 0) {
			buf_->delete_row(node->row);
			buf_->SetCursor(0, static_cast<std::size_t>(node->row));
		} else {
			buf_->insert_row(node->row, node->text);
			buf_->SetCursor(0, static_cast<std::size_t>(node->row));
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