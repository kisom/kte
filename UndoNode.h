#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "TextSpan.h"


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
	// A large Insert/Paste/Delete text is held as references to the buffer's
	// storage instead (text is then empty and span_len is its length): undo
	// of a replace-all over a 100 MB file otherwise kept 200 MB of copies.
	std::vector<TextSpan> spans;
	std::size_t span_len = 0;
	UndoNode *parent = nullptr; // previous state; null means pre-first-edit
	UndoNode *child  = nullptr; // next in current timeline
	UndoNode *next   = nullptr; // redo branch


	[[nodiscard]] bool HasSpans() const
	{
		return !spans.empty();
	}


	// Length of the node's text, however it is held.
	[[nodiscard]] std::size_t TextSize() const
	{
		return HasSpans() ? span_len : text.size();
	}


	// Approximate heap and object bytes held by this node.
	[[nodiscard]] std::size_t CostBytes() const
	{
		return sizeof(UndoNode) + text.capacity() + spans.capacity() * sizeof(TextSpan);
	}
};