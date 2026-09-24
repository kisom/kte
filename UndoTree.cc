// Undo logic is implemented in UndoSystem; this file only owns node lifetime.
#include "UndoTree.h"

#include <vector>


void
FreeUndoForest(UndoNode *first)
{
	// Nodes form a first-child/next-sibling tree whose depth equals the
	// length of the edit history, so walk it with an explicit stack.
	std::vector<UndoNode *> stack;
	if (first)
		stack.push_back(first);
	while (!stack.empty()) {
		UndoNode *node = stack.back();
		stack.pop_back();
		if (node->child)
			stack.push_back(node->child);
		if (node->next)
			stack.push_back(node->next);
		delete node;
	}
}


UndoTree::~UndoTree()
{
	FreeUndoForest(root);
	delete pending;
}
