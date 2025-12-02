/*
 * LspTypes.h - minimal LSP-related data types for initial integration
 */
#ifndef KTE_LSP_TYPES_H
#define KTE_LSP_TYPES_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kte::lsp {
struct Position {
	int line      = 0;
	int character = 0;
};

struct Range {
	Position start;
	Position end;
};

struct TextDocumentContentChangeEvent {
	std::optional<Range> range; // if not set, represents full document change
	std::string text; // new text for the given range
};
} // namespace kte::lsp

#endif // KTE_LSP_TYPES_H