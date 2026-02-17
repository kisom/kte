#pragma once
#include <string_view>
#include <cstddef>
#include <cstdint>

#include "UndoTree.h"


class Buffer;

class UndoSystem {
public:
	explicit UndoSystem(Buffer &owner, UndoTree &tree);

	// Begin an atomic group: subsequent committed nodes with the same group_id will be
	// undone/redone as a single step. Returns the active group id.
	std::uint64_t BeginGroup();

	void EndGroup();

	void Begin(UndoType type);

	void Append(char ch);

	void Append(std::string_view text);

	void commit();

	void undo();

	// Redo the current node's active child branch.
	// If `branch_index` > 0, selects that redo sibling (0-based) and makes it active.
	// When current is null (pre-first-edit), branches are selected among `tree_.root` siblings.
	void redo(int branch_index = 0);

	void mark_saved();

	void discard_pending();

	void clear();

	void UpdateBufferReference(Buffer &new_buf);

#if defined(KTE_TESTS)
	// Test-only introspection hook.
	const UndoTree &TreeForTests() const
	{
		return tree_;
	}
#endif

private:
	enum class PendingAppendMode : std::uint8_t {
		Append,
		Prepend,
	};

	void apply(const UndoNode *node, int direction); // +1 redo, -1 undo
	void free_node(UndoNode *node);

	void free_branch(UndoNode *node); // frees redo siblings only
	UndoNode *find_parent(UndoNode *from, UndoNode *target);

	// Debug helpers (compiled only when KTE_UNDO_DEBUG is defined)
	void debug_log(const char *op) const;

	static const char *type_str(UndoType t);

	static bool is_descendant(UndoNode *root, const UndoNode *target);

	void update_dirty_flag();

	PendingAppendMode pending_mode_ = PendingAppendMode::Append;

	std::uint64_t active_group_id_ = 0;
	std::uint64_t next_group_id_   = 1;

	Buffer *buf_;
	UndoTree &tree_;
};
