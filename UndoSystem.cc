#include "UndoSystem.h"
#include "Buffer.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>


UndoSystem::UndoSystem(Buffer &owner, UndoTree &tree)
	: buf_(&owner), tree_(tree) {}


std::uint64_t
UndoSystem::BeginGroup()
{
	// Ensure any pending typed run is sealed so the group is a distinct undo step.
	commit();
	if (active_group_id_ == 0)
		active_group_id_ = next_group_id_++;
	// Groups nest (e.g. SmartNewline wraps Newline, which groups its own
	// visual-line splits); only the outermost EndGroup closes the group.
	++group_depth_;
	return active_group_id_;
}


void
UndoSystem::EndGroup()
{
	commit();
	if (group_depth_ > 0)
		--group_depth_;
	if (group_depth_ == 0) {
		active_group_id_ = 0;
		maybe_prune();
	}
}


void
UndoSystem::Begin(UndoType type)
{
	if (!buf_)
		return;
	const int row = static_cast<int>(buf_->Cury());
	const int col = static_cast<int>(buf_->Curx());

	// Some operations should always be standalone undo steps.
	const bool always_standalone = (type == UndoType::Newline || type == UndoType::DeleteRow || type ==
	                                UndoType::InsertRow || type == UndoType::JoinLines);
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
			case UndoType::InsertRow:
			case UndoType::JoinLines:
				break;
			}
		}
		// Can't coalesce: seal the previous pending step.
		commit();
	}

	// Start a new pending node.
	tree_.pending           = new UndoNode{};
	tree_.pending->type     = type;
	tree_.pending->row      = row;
	tree_.pending->col      = col;
	tree_.pending->group_id = active_group_id_;
	tree_.pending->text.clear();
	tree_.pending->parent = nullptr;
	tree_.pending->child  = nullptr;
	tree_.pending->next   = nullptr;
	pending_mode_         = PendingAppendMode::Append;
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

	try_convert_to_spans(tree_.pending);
	bytes_ += tree_.pending->CostBytes();

	if (!tree_.root) {
		tree_.root            = tree_.pending;
		tree_.pending->parent = nullptr;
		tree_.current         = tree_.pending;
	} else if (!tree_.current) {
		// We are at the "pre-first-edit" state (undo past the first node).
		// In branching history, preserve the existing root chain as an alternate branch.
		tree_.pending->parent = nullptr;
		tree_.pending->next   = tree_.root;
		tree_.root            = tree_.pending;
		tree_.current         = tree_.pending;
	} else {
		// Branching semantics: attach as a new redo branch under current.
		// Make the new edit the active child by inserting it at the head.
		tree_.pending->parent = tree_.current;
		if (!tree_.current->child) {
			tree_.current->child = tree_.pending;
		} else {
			tree_.pending->next  = tree_.current->child;
			tree_.current->child = tree_.pending;
		}
		tree_.current = tree_.pending;
	}

	tree_.pending = nullptr;
	pending_mode_ = PendingAppendMode::Append;
	update_dirty_flag();
	if (group_depth_ == 0)
		maybe_prune();
}


void
UndoSystem::undo()
{
	// Seal any in-progress typed run before undo.
	commit();
	if (!tree_.current)
		return;
	debug_log("undo");
	const std::uint64_t gid = tree_.current->group_id;
	do {
		UndoNode *node = tree_.current;
		apply(node, -1);
		tree_.current = node->parent;
	} while (gid != 0 && tree_.current && tree_.current->group_id == gid);
	update_dirty_flag();
}


void
UndoSystem::redo(int branch_index)
{
	commit();
	UndoNode **head = nullptr;
	if (!tree_.current) {
		head = &tree_.root;
	} else {
		head = &tree_.current->child;
	}
	if (!head || !*head)
		return;
	if (branch_index < 0)
		branch_index = 0;

	// Select the Nth sibling from the branch list and make it the active head.
	UndoNode *prev = nullptr;
	UndoNode *sel  = *head;
	for (int i = 0; i < branch_index && sel; ++i) {
		prev = sel;
		sel  = sel->next;
	}
	if (!sel)
		return;
	if (prev) {
		prev->next = sel->next;
		sel->next  = *head;
		*head      = sel;
	}

	debug_log("redo");
	UndoNode *node          = *head;
	const std::uint64_t gid = node->group_id;
	apply(node, +1);
	tree_.current = node;
	while (gid != 0 && tree_.current && tree_.current->child
	       && tree_.current->child->group_id == gid) {
		UndoNode *child = tree_.current->child;
		apply(child, +1);
		tree_.current = child;
	}
	update_dirty_flag();
}


void
UndoSystem::mark_saved()
{
	commit();
	tree_.saved   = tree_.current;
	base_unsaved_ = false;
	update_dirty_flag();
}


void
UndoSystem::AbortGroups()
{
	commit();
	group_depth_     = 0;
	active_group_id_ = 0;
}


void
UndoSystem::mark_base_unsaved()
{
	base_unsaved_ = true;
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
	// The root may have siblings (edits made after undoing to the start).
	FreeUndoForest(tree_.root);
	tree_.root       = nullptr;
	tree_.current    = nullptr;
	tree_.saved      = nullptr;
	base_unsaved_    = false;
	bytes_           = 0;
	active_group_id_ = 0;
	group_depth_     = 0;
	next_group_id_   = 1;
	update_dirty_flag();
}


// Position just after the node's text inserted at (node->col, node->row).
// The text may span lines: the column is then the length after its last
// newline, not col + size (which left the cursor past the end of the line,
// so later edits and their undo records went to the wrong place).
static void
end_of_text(const Buffer &buf, const UndoNode *node, std::size_t &x, std::size_t &y)
{
	y = static_cast<std::size_t>(node->row);
	x = static_cast<std::size_t>(node->col);
	auto scan = [&](const char *d, std::size_t n) {
		for (std::size_t i = 0; i < n; ++i) {
			if (d[i] == '\n') {
				++y;
				x = 0;
			} else {
				++x;
			}
		}
	};
	if (node->HasSpans())
		buf.VisitSpans(node->spans, scan);
	else
		scan(node->text.data(), node->text.size());
}


// Insert the node's text at its position (from spans without copying).
static void
insert_node_text(Buffer &buf, const UndoNode *node)
{
	if (node->HasSpans())
		buf.insert_spans(node->row, node->col, node->spans);
	else
		buf.insert_text(node->row, node->col, node->text);
}


void
UndoSystem::apply(const UndoNode *node, int direction)
{
	std::size_t ex = 0, ey = 0;
	if (!node)
		return;
	// Cursor positioning: keep the point at a sensible location after undo/redo.
	// Low-level Buffer edit primitives do not move the cursor.
	switch (node->type) {
	case UndoType::Insert:
	case UndoType::Paste:
		if (direction > 0) {
			insert_node_text(*buf_, node);
			end_of_text(*buf_, node, ex, ey);
			buf_->SetCursor(ex, ey);
		} else {
			buf_->delete_text(node->row, node->col, node->TextSize());
			buf_->SetCursor(static_cast<std::size_t>(node->col), static_cast<std::size_t>(node->row));
		}
		break;
	case UndoType::Delete:
		if (direction > 0) {
			buf_->delete_text(node->row, node->col, node->TextSize());
			buf_->SetCursor(static_cast<std::size_t>(node->col), static_cast<std::size_t>(node->row));
		} else {
			insert_node_text(*buf_, node);
			end_of_text(*buf_, node, ex, ey);
			buf_->SetCursor(ex, ey);
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
	case UndoType::InsertRow:
		if (direction > 0) {
			buf_->insert_row(node->row, node->text);
			buf_->SetCursor(0, static_cast<std::size_t>(node->row));
		} else {
			buf_->delete_row(node->row);
			buf_->SetCursor(0, static_cast<std::size_t>(node->row));
		}
		break;
	case UndoType::JoinLines:
		// Mirror image of Newline: forward removes a newline, backward restores it.
		if (direction > 0) {
			buf_->join_lines(node->row);
			buf_->SetCursor(static_cast<std::size_t>(node->col), static_cast<std::size_t>(node->row));
		} else {
			buf_->split_line(node->row, node->col);
			buf_->SetCursor(0, static_cast<std::size_t>(node->row + 1));
		}
		break;
	}
}


void
UndoSystem::free_node(UndoNode *node)
{
	// Free this node and its child subtree, but not its siblings.
	if (!node)
		return;
	UndoNode *children = node->child;
	node->child        = nullptr;
	delete node;
	FreeUndoForest(children);
}


void
UndoSystem::free_branch(UndoNode *node)
{
	// Free a branch list (node and its next siblings) including their subtrees
	FreeUndoForest(node);
}


// Iterative search for `target` below `cur`; sets out_parent to its parent.
static bool
dfs_find_parent(UndoNode *cur, UndoNode *target, UndoNode *&out_parent)
{
	if (!cur)
		return false;
	std::vector<UndoNode *> stack{cur};
	while (!stack.empty()) {
		UndoNode *node = stack.back();
		stack.pop_back();
		for (UndoNode *child = node->child; child != nullptr; child = child->next) {
			if (child == target) {
				out_parent = node;
				return true;
			}
			stack.push_back(child);
		}
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


// Texts at least this long are held as storage spans when possible.
static constexpr std::size_t kSpanMinBytes = PieceTable::kCaptureDeletedMin;


void
UndoSystem::try_convert_to_spans(UndoNode *node)
{
	if (!buf_ || !node || node->text.size() < kSpanMinBytes)
		return;
	std::vector<TextSpan> spans;
	switch (node->type) {
	case UndoType::Insert:
	case UndoType::Paste:
		// The inserted text is in the buffer at the node's position.
		spans = buf_->SpansAt(node->row, node->col, node->text.size());
		break;
	case UndoType::Delete:
		// The deleted text is still in storage; the deletion remembered where.
		spans = buf_->TakeDeletedSpans(node->row, node->col, node->text.size());
		break;
	default:
		return;
	}
	// Only when the spans hold exactly the recorded text; otherwise (the
	// text is not where the node says) keep the copy.
	if (spans.empty() || !buf_->SpansEqual(spans, node->text))
		return;
	node->span_len = node->text.size();
	node->spans    = std::move(spans);
	node->spans.shrink_to_fit();
	std::string().swap(node->text);
}


void
UndoSystem::maybe_prune()
{
	std::size_t budget = budget_;
	if (budget == 0) {
		const std::size_t content = buf_ ? buf_->ContentBytes() : 0;
		budget                    = std::max<std::size_t>(std::size_t{64} << 20, content * 2);
	}
	if (bytes_ <= budget)
		return;
	// Prune well below the budget so this does not run on every commit.
	prune_to(budget - budget / 4);
}


void
UndoSystem::forget_saved_state()
{
	tree_.saved   = nullptr;
	base_unsaved_ = true;
}


// Total bytes held by a node, its children and its siblings.
static std::size_t
forest_bytes(const UndoNode *first)
{
	std::size_t total = 0;
	std::vector<const UndoNode *> stack;
	if (first)
		stack.push_back(first);
	while (!stack.empty()) {
		const UndoNode *n = stack.back();
		stack.pop_back();
		total += n->CostBytes();
		if (n->child)
			stack.push_back(n->child);
		if (n->next)
			stack.push_back(n->next);
	}
	return total;
}


static bool
forest_contains(const UndoNode *first, const UndoNode *target)
{
	if (!target)
		return false;
	std::vector<const UndoNode *> stack;
	if (first)
		stack.push_back(first);
	while (!stack.empty()) {
		const UndoNode *n = stack.back();
		stack.pop_back();
		if (n == target)
			return true;
		if (n->child)
			stack.push_back(n->child);
		if (n->next)
			stack.push_back(n->next);
	}
	return false;
}


void
UndoSystem::prune_to(const std::size_t target)
{
	// The line kept: root..current, then current's active redo line.
	std::vector<UndoNode *> line;
	for (UndoNode *n = tree_.current; n; n = n->parent)
		line.push_back(n);
	std::reverse(line.begin(), line.end());
	{
		UndoNode *n = tree_.current ? tree_.current->child : tree_.root;
		for (; n; n = n->child)
			line.push_back(n);
	}
	if (line.empty())
		return;

	// Detach and free the siblings of `keep` in the list starting at *head.
	auto drop_siblings = [&](UndoNode **head, UndoNode *keep) {
		UndoNode *n = *head;
		*head       = keep;
		while (n) {
			UndoNode *nx = n->next;
			if (n != keep) {
				n->next = nullptr;
				if (forest_contains(n, tree_.saved))
					forget_saved_state();
				FreeUndoForest(n);
			}
			n = nx;
		}
		keep->next = nullptr;
	};

	// 1. Branches off the kept line: reachable only by choosing a redo branch.
	drop_siblings(&tree_.root, line.front());
	for (std::size_t i = 0; i + 1 < line.size(); ++i)
		drop_siblings(&line[i]->child, line[i + 1]);
	bytes_ = forest_bytes(tree_.root);

	// 2. The oldest edits, while they are older than the current state. An
	// undo group goes whole: undoing what remained of it would stop at a
	// state inside the group, which the user never saw.
	std::size_t i  = 0;
	bool mid_group = false;
	while ((bytes_ > target || mid_group) && i < line.size() && line[i] != tree_.current && tree_.current) {
		UndoNode *old = line[i];
		UndoNode *nx  = old->child;
		if (!nx)
			break;
		if (old->group_id != 0 && old->group_id == tree_.current->group_id)
			break; // the current state's own group stays whole
		// Dropping `old` loses the state before it. `old`'s own state
		// becomes the first state in history.
		if (tree_.saved == nullptr)
			forget_saved_state();
		else if (tree_.saved == old)
			tree_.saved = nullptr;
		mid_group  = old->group_id != 0 && nx->group_id == old->group_id;
		bytes_     -= old->CostBytes();
		old->child = nullptr;
		delete old;
		nx->parent = nullptr;
		tree_.root = nx;
		++i;
	}

	// 3. The redo line beyond the current state, newest first.
	if (bytes_ > target && tree_.current && tree_.current->child) {
		UndoNode *future = tree_.current->child;
		if (forest_contains(future, tree_.saved))
			forget_saved_state();
		tree_.current->child = nullptr;
		bytes_               -= forest_bytes(future);
		FreeUndoForest(future);
	}
	update_dirty_flag();
}


void
UndoSystem::update_dirty_flag()
{
	// dirty if current != saved
	bool dirty = base_unsaved_ || (tree_.current != tree_.saved);
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
	case UndoType::InsertRow:
		return "InsertRow";
	case UndoType::JoinLines:
		return "JoinLines";
	}
	return "?";
}


bool
UndoSystem::is_descendant(UndoNode *root, const UndoNode *target)
{
	if (!root || !target)
		return false;
	std::vector<UndoNode *> stack{root};
	while (!stack.empty()) {
		UndoNode *node = stack.back();
		stack.pop_back();
		if (node == target)
			return true;
		for (UndoNode *child = node->child; child != nullptr; child = child->next)
			stack.push_back(child);
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