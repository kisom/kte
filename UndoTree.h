#ifndef KTE_UNDOTREE_H
#define KTE_UNDOTREE_H

#include "UndoNode.h"

class UndoTree {
	UndoTree() : root{nullptr}, current{nullptr}, pending{nullptr} {}

	void Begin(UndoKind kind, size_t row, size_t col);

	void Commit();

private:
	UndoNode *root{nullptr};
	UndoNode *current{nullptr};
	UndoNode *pending{nullptr};
};


#endif // KTE_UNDOTREE_H
