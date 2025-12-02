// CppHighlighter.h - minimal stateless C/C++ line highlighter
#pragma once

#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include "LanguageHighlighter.h"

class Buffer;

namespace kte {

class CppHighlighter final : public StatefulHighlighter {
public:
    CppHighlighter();
    ~CppHighlighter() override = default;

    void HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const override;
    LineState HighlightLineStateful(const Buffer &buf,
                                    int row,
                                    const LineState &prev,
                                    std::vector<HighlightSpan> &out) const override;

private:
    std::unordered_set<std::string> keywords_;
    std::unordered_set<std::string> types_;

    static bool is_ident_start(char c);
    static bool is_ident_char(char c);
};

} // namespace kte
