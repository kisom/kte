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
	std::string text;
	UndoNode *child = nullptr; // next in current timeline
	UndoNode *next  = nullptr; // redo branch
};