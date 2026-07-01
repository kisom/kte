// Undo logic is implemented in UndoSystem; this file only owns node lifetime.
#include "UndoTree.h"

namespace {
void
free_node_graph(UndoNode *node)
{
	// Walk the sibling (redo-branch) list; for each node, recursively free its
	// child subtree first, then the node itself.
	while (node) {
		UndoNode *next = node->next;
		free_node_graph(node->child);
		delete node;
		node = next;
	}
}
} // namespace


UndoTree::~UndoTree()
{
	free_node_graph(root);
	delete pending;
}
