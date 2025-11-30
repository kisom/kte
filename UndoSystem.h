#ifndef KTE_UNDOSYSTEM_H
#define KTE_UNDOSYSTEM_H

#include <string_view>
#include "UndoTree.h"

class Buffer;

class UndoSystem {
public:
	explicit UndoSystem(Buffer &owner, UndoTree &tree);

	void Begin(UndoType type);

	void Append(char ch);

	void Append(std::string_view text);

	void commit();

	void undo();

	void redo();

	void mark_saved();

	void discard_pending();

	void clear();

private:
	void apply(const UndoNode *node, int direction); // +1 redo, -1 undo
	void free_node(UndoNode *node);

	void free_branch(UndoNode *node); // frees redo siblings only
	UndoNode *find_parent(UndoNode *from, UndoNode *target);

	void update_dirty_flag();

private:
	Buffer &buf_;
	UndoTree &tree_;
	// Internal hint for Delete batching: whether next Append() should prepend
	bool pending_prepend_ = false;
};

#endif // KTE_UNDOSYSTEM_H
