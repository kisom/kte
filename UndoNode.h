#pragma once
#include <cstdint>
#include <string>


enum class UndoType : std::uint8_t {
	Insert,
	Delete,
	Paste,
	Newline,
	DeleteRow,
	InsertRow,
	// Inverse of Newline: forward = join_lines(row) [removes the newline at the
	// end of `row`], backward = split_line(row, col) [recreates the original
	// two lines]. Used by backspace-at-col-0 and delete-at-eol, whose forward
	// action is a join, not a split — Newline's apply() semantics are the
	// wrong direction for those.
	JoinLines,
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