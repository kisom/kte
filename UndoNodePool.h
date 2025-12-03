#ifndef KTE_UNDONODEPOOL_H
#define KTE_UNDONODEPOOL_H

#include <stack>
#include <vector>
#include <memory>
#include "UndoNode.h"

// Pool allocator for UndoNode to eliminate frequent malloc/free.
// Uses fixed-size blocks to keep node addresses stable.
class UndoNodePool {
public:
	explicit UndoNodePool(std::size_t block_size = 64)
		: block_size_(block_size) {}


	UndoNode *acquire()
	{
		if (available_.empty())
			allocate_block();
		auto *node = available_.top();
		available_.pop();
		// Node comes zeroed; ensure links are reset
		node->text.clear();
		node->child = nullptr;
		node->next  = nullptr;
		node->row   = node->col = 0;
		node->type  = UndoType{};
		return node;
	}


	void release(UndoNode *node)
	{
		if (!node)
			return;
		// Clear heavy fields to free memory held by strings
		node->text.clear();
		node->child = nullptr;
		node->next  = nullptr;
		node->row   = node->col = 0;
		node->type  = UndoType{};
		available_.push(node);
	}

private:
	void allocate_block()
	{
		// allocate a new block; keep ownership so memory stays valid
		std::unique_ptr<UndoNode[]> block(new UndoNode[block_size_]);
		UndoNode *base = block.get();
		blocks_.push_back(std::move(block));
		for (std::size_t i = 0; i < block_size_; ++i) {
			// ensure the node is reset; rely on default constructor/zero init
			available_.push(&base[i]);
		}
	}


	std::size_t block_size_;
	std::vector<std::unique_ptr<UndoNode[]> > blocks_;
	std::stack<UndoNode *> available_;
};

#endif // KTE_UNDONODEPOOL_H