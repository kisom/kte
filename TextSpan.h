/*
 * TextSpan.h - a reference to bytes held in a PieceTable's storage.
 *
 * PieceTable storage is append-only (edits add pieces; nothing rewrites
 * stored bytes), so a span stays valid for the life of the table's storage.
 * Undo records large texts as spans instead of copies of the bytes.
 */
#pragma once
#include <cstddef>


struct TextSpan {
	bool add{false}; // false: original storage, true: add storage
	std::size_t start{0};
	std::size_t len{0};
};
