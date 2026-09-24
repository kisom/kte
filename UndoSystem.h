/*
 * UndoSystem.h - undo/redo system with tree-based branching
 *
 * UndoSystem manages the undo/redo history for a Buffer. It provides:
 *
 * - Tree-based undo: Multiple redo branches at each node (not just linear history)
 * - Atomic grouping: Multiple operations can be undone/redone as a single step
 * - Dirty tracking: Marks when buffer matches last saved state
 * - Efficient storage: Nodes stored in UndoTree, operations applied to Buffer
 *
 * Key concepts:
 *
 * 1. Undo tree structure:
 *    - Each edit creates a node in the tree
 *    - Undo moves up the tree (toward root)
 *    - Redo moves down the tree (toward leaves)
 *    - Multiple redo branches preserved (not lost on new edits after undo)
 *
 * 2. Operation lifecycle:
 *    - Begin(type): Start recording an operation (insert/delete)
 *    - Append(text): Add content to the pending operation
 *    - commit(): Finalize and add to undo tree
 *    - discard_pending(): Cancel without recording
 *
 * 3. Atomic grouping:
 *    - BeginGroup()/EndGroup(): Bracket multiple operations
 *    - All operations in a group share the same group_id
 *    - Undo/redo treats the entire group as one step
 *
 * 4. Integration with Buffer:
 *    - UndoSystem holds a reference to its owning Buffer
 *    - apply() executes undo/redo by calling Buffer's editing methods
 *    - Buffer's dirty flag updated automatically
 *
 * Usage pattern:
 *   undo_system.Begin(UndoType::Insert);
 *   undo_system.Append("text");
 *   undo_system.commit();  // Now undoable
 *
 * See also: UndoTree.h (storage), UndoNode.h (node structure)
 */
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

	// No undo state matches the file on disk any more (e.g. after crash
	// recovery replayed edits onto it): the buffer stays dirty in every state
	// until the next mark_saved() or clear().
	void mark_base_unsaved();

	// Close any open group (after an interrupted command) and seal pending.
	void AbortGroups();

	void discard_pending();

	void clear();

	void UpdateBufferReference(Buffer &new_buf);

	// History memory budget in bytes (node texts, spans and nodes). When
	// committed history exceeds it, branches off the current path and then
	// the oldest edits are dropped. 0 (the default) means max(64 MiB, twice
	// the buffer's size).
	void SetByteBudget(std::size_t bytes)
	{
		budget_ = bytes;
	}


	[[nodiscard]] std::size_t HistoryBytes() const
	{
		return bytes_;
	}

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

	// Hold a large committed text as storage spans (see UndoNode::spans).
	void try_convert_to_spans(UndoNode *node);

	// Enforce the byte budget (outside groups).
	void maybe_prune();

	void prune_to(std::size_t target);

	// The saved state's node is being dropped: no state in history matches
	// the file any more.
	void forget_saved_state();

	std::size_t bytes_  = 0; // approximate bytes held by committed history
	std::size_t budget_ = 0;

	PendingAppendMode pending_mode_ = PendingAppendMode::Append;

	std::uint64_t active_group_id_ = 0;
	int group_depth_                = 0; // nesting depth of BeginGroup/EndGroup
	bool base_unsaved_               = false; // see mark_base_unsaved()
	std::uint64_t next_group_id_   = 1;

	Buffer *buf_;
	UndoTree &tree_;
};