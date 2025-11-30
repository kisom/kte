#include "UndoSystem.h"
#include "Buffer.h"


UndoSystem::UndoSystem(Buffer &owner, UndoTree &tree)
	: buf_(owner), tree_(tree) {}


void
UndoSystem::Begin(UndoType type)
{
	// Reuse pending if batching conditions are met
	const int row = static_cast<int>(buf_.Cury());
	const int col = static_cast<int>(buf_.Curx());
	if (tree_.pending && tree_.pending->type == type && tree_.pending->row == row) {
		if (type == UndoType::Delete) {
			// Support batching both forward deletes (DeleteChar) and backspace (prepend case)
			// Forward delete: cursor stays at anchor col; expected == col
			std::size_t anchor = static_cast<std::size_t>(tree_.pending->col);
			if (anchor + tree_.pending->text.size() == static_cast<std::size_t>(col)) {
				pending_prepend_ = false;
				return; // keep batching forward delete
			}
			// Backspace: cursor moved left by 1; allow extend if col + text.size() == anchor
			if (static_cast<std::size_t>(col) + tree_.pending->text.size() == anchor) {
				// Move anchor one left to new cursor column; next Append should prepend
				tree_.pending->col = col;
				pending_prepend_   = true;
				return;
			}
		} else {
			std::size_t expected = static_cast<std::size_t>(tree_.pending->col) + tree_.pending->text.
			                       size();
			if (expected == static_cast<std::size_t>(col)) {
				pending_prepend_ = false;
				return; // keep batching
			}
		}
	}
	// Otherwise commit any existing batch and start a new node
	commit();
	auto *node       = new UndoNode();
	node->type       = type;
	node->row        = row;
	node->col        = col;
	node->child      = nullptr;
	node->next       = nullptr;
	tree_.pending    = node;
	pending_prepend_ = false;
}


void
UndoSystem::Append(char ch)
{
	if (!tree_.pending)
		return;
	if (pending_prepend_ && tree_.pending->type == UndoType::Delete) {
		// Prepend for backspace so that text is in increasing column order
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
	tree_.pending->text.append(text.data(), text.size());
}


void
UndoSystem::commit()
{
	if (!tree_.pending)
		return;

	// If we have redo branches from current, discard them (non-linear behavior)
	if (tree_.current && tree_.current->child) {
		free_node(tree_.current->child);
		tree_.current->child = nullptr;
		// We diverged; saved snapshot cannot be on discarded branch anymore
		if (tree_.saved) {
			// If saved is not equal to current, keep it; if it was on discarded branch we cannot easily detect now.
			// For simplicity, leave saved as-is; dirty flag uses pointer equality.
		}
	}

	// Attach pending as next state
	if (!tree_.root) {
		tree_.root    = tree_.pending;
		tree_.current = tree_.pending;
	} else if (!tree_.current) {
		// Should not happen if root exists, but handle gracefully
		tree_.current = tree_.pending;
	} else {
		// Attach as primary child (head of redo list)
		tree_.pending->next  = nullptr;
		tree_.current->child = tree_.pending;
		tree_.current        = tree_.pending;
	}
	tree_.pending = nullptr;
	update_dirty_flag();
}


void
UndoSystem::undo()
{
	// Close any pending batch
	commit();
	if (!tree_.current)
		return;
	UndoNode *parent = find_parent(tree_.root, tree_.current);
	UndoNode *node   = tree_.current;
	// Apply inverse of current node
	apply(node, -1);
	tree_.current = parent;
	update_dirty_flag();
}


void
UndoSystem::redo()
{
	// Redo next child along current timeline
	if (tree_.pending) {
		// If app added pending edits, finalize them before redo chain
		commit();
	}
	UndoNode *next = nullptr;
	if (!tree_.current) {
		next = tree_.root; // if nothing yet, try applying first node
	} else {
		next = tree_.current->child;
	}
	if (!next)
		return;
	apply(next, +1);
	tree_.current = next;
	update_dirty_flag();
}


void
UndoSystem::mark_saved()
{
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
}


void
UndoSystem::clear()
{
	if (tree_.root) {
		free_node(tree_.root);
	}
	if (tree_.pending) {
		delete tree_.pending;
	}
	tree_.root = tree_.current = tree_.saved = tree_.pending = nullptr;
	update_dirty_flag();
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
			buf_.insert_text(node->row, node->col, node->text);
		} else {
			buf_.delete_text(node->row, node->col, node->text.size());
		}
		break;
	case UndoType::Delete:
		if (direction > 0) {
			buf_.delete_text(node->row, node->col, node->text.size());
		} else {
			buf_.insert_text(node->row, node->col, node->text);
		}
		break;
	case UndoType::Newline:
		if (direction > 0) {
			buf_.split_line(node->row, node->col);
		} else {
			buf_.join_lines(node->row);
		}
		break;
	case UndoType::DeleteRow:
		if (direction > 0) {
			buf_.delete_row(node->row);
		} else {
			buf_.insert_row(node->row, node->text);
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
	buf_.SetDirty(dirty);
}
