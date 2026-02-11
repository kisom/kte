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
	UndoNode *parent = nullptr; // previous state; null means pre-first-edit
	UndoNode *child  = nullptr; // next in current timeline
	UndoNode *next   = nullptr; // redo branch
};