#pragma once
#include "UndoNode.h"


struct UndoTree {
	UndoNode *root    = nullptr; // first edit ever
	UndoNode *current = nullptr; // current state of buffer
	UndoNode *saved   = nullptr; // points to node matching last save (for dirty flag)
	UndoNode *pending = nullptr; // in-progress batch (detached)
};