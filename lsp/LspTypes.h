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
// NOTE on coordinates:
// - Internal editor coordinates use UTF-8 columns counted by Unicode scalars.
// - LSP wire protocol uses UTF-16 code units for the `character` field.
//   Conversions are performed in higher layers via `lsp/UtfCodec.h` helpers.
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