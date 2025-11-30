#ifndef KTE_UNDOTREE_H
#define KTE_UNDOTREE_H

#include "UndoNode.h"
#include <memory>

struct UndoTree {
	UndoNode *root    = nullptr; // first edit ever
	UndoNode *current = nullptr; // current state of buffer
	UndoNode *saved   = nullptr; // points to node matching last save (for dirty flag)
	UndoNode *pending = nullptr; // in-progress batch (detached)
};


#endif // KTE_UNDOTREE_H
