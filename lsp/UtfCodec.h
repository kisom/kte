/*
 * UtfCodec.h - Helpers for UTF-8 <-> UTF-16 code unit position conversions
 */
#ifndef KTE_LSP_UTF_CODEC_H
#define KTE_LSP_UTF_CODEC_H

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "LspTypes.h"

namespace kte::lsp {
// Map between editor-internal UTF-8 columns (by Unicode scalar count)
// and LSP wire UTF-16 code units (per LSP spec).

// Convert a UTF-8 column index (in Unicode scalars) to UTF-16 code units for a given line.
size_t utf8ColToUtf16Units(std::string_view lineUtf8, size_t utf8Col);

// Convert a UTF-16 code unit count to a UTF-8 column index (in Unicode scalars) for a given line.
size_t utf16UnitsToUtf8Col(std::string_view lineUtf8, size_t utf16Units);

// Line text provider to allow conversions without giving the codec direct buffer access.
using LineProvider = std::function<std::string_view(const std::string & uri, int line)>;

// Convenience helpers for positions and ranges using a line provider.
Position toUtf16(const std::string &uri, const Position &pUtf8, const LineProvider &provider);

Position toUtf8(const std::string &uri, const Position &pUtf16, const LineProvider &provider);

Range toUtf16(const std::string &uri, const Range &rUtf8, const LineProvider &provider);

Range toUtf8(const std::string &uri, const Range &rUtf16, const LineProvider &provider);
} // namespace kte::lsp

#endif // KTE_LSP_UTF_CODEC_H