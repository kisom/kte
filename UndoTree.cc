#include "UndoTree.h"

#include <cassert>


void
UndoTree::Begin(const UndoKind kind, const size_t row, const size_t col)
{
	if (this->pending != nullptr) {
		if (this->pending->Kind() == kind) {
			return;
		}
		this->Commit();
	}

	assert(this->pending == nullptr);
	this->pending = new UndoNode(kind, row, col);
	assert(this->pending != nullptr);
}


void
UndoTree::Commit()
{
	if (this->pending == nullptr) {
		return;
	}

	if (this->root == nullptr) {
		assert(this->current == nullptr);

		this->root    = this->pending;
		this->current = this->pending;
		this->pending = nullptr;

		return;
	}

	assert(this->current != nullptr);
	if (this->current->Next() != nullptr) {
		this->current->DeleteNext();
	}

	this->current->Next(this->pending);
	this->current = this->pending;
	this->pending = nullptr;
}
