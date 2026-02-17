#pragma once
#include <cstdint>
#include <string>


enum class UndoType : std::uint8_t {
	Insert,
	Delete,
	Paste,
	Newline,
	DeleteRow,
};

struct UndoNode {
	UndoType type{};
	int row{};
	int col{};
	std::uint64_t group_id = 0; // 0 means ungrouped; non-zero means undo/redo as an atomic group
	std::string text;
	UndoNode *parent = nullptr; // previous state; null means pre-first-edit
	UndoNode *child  = nullptr; // next in current timeline
	UndoNode *next   = nullptr; // redo branch
};
