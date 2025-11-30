#include "UndoNode.h"


void
UndoNode::DeleteNext() const
{
	const UndoNode *node = next_;
	const UndoNode *next = nullptr;

	while (node != nullptr) {
		next = node->Next();
		delete node;
		node = next;
	}
}
