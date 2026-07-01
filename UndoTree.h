#pragma once
#include "UndoNode.h"


struct UndoTree {
	UndoNode *root    = nullptr; // first edit ever
	UndoNode *current = nullptr; // current state of buffer
	UndoNode *saved   = nullptr; // points to node matching last save (for dirty flag)
	UndoNode *pending = nullptr; // in-progress batch (detached)

	// Frees the entire node graph (root's subtree/branches plus any detached
	// pending node). `current`/`saved` are aliases into that same graph, not
	// separately owned. Without this, closing/reloading/resetting a buffer
	// (which replaces or destroys its UndoTree) leaks every UndoNode ever
	// created for it.
	~UndoTree();
};
