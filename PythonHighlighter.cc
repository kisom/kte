#include "PythonHighlighter.h"
#include "Buffer.h"
#include <cctype>

namespace kte {

static void push(std::vector<HighlightSpan> &out, int a, int b, TokenKind k){ if (b>a) out.push_back({a,b,k}); }
static bool is_ident_start(char c){ return std::isalpha(static_cast<unsigned char>(c)) || c=='_'; }
static bool is_ident_char(char c){ return std::isalnum(static_cast<unsigned char>(c)) || c=='_'; }

PythonHighlighter::PythonHighlighter()
{
    const char* kw[] = {"and","as","assert","break","class","continue","def","del","elif","else","except","False","finally","for","from","global","if","import","in","is","lambda","None","nonlocal","not","or","pass","raise","return","True","try","while","with","yield"};
    for (auto s: kw) kws_.insert(s);
}

void PythonHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
    LineState st; (void)HighlightLineStateful(buf, row, st, out);
}

StatefulHighlighter::LineState PythonHighlighter::HighlightLineStateful(const Buffer &buf, int row, const LineState &prev, std::vector<HighlightSpan> &out) const
{
    StatefulHighlighter::LineState state = prev;
    const auto &rows = buf.Rows();
    if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) return state;
    std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
    int n = static_cast<int>(s.size());

    // Triple-quoted string continuation uses in_raw_string with raw_delim either "'''" or "\"\"\""
    if (state.in_raw_string && (state.raw_delim == "'''" || state.raw_delim == "\"\"\"")) {
        auto pos = s.find(state.raw_delim);
        if (pos == std::string::npos) {
            push(out, 0, n, TokenKind::String);
            return state; // still inside
        } else {
            int end = static_cast<int>(pos + static_cast<int>(state.raw_delim.size()));
            push(out, 0, end, TokenKind::String);
            // remainder processed normally
            s = s.substr(end);
            n = static_cast<int>(s.size());
            state.in_raw_string = false; state.raw_delim.clear();
            // Continue parsing remainder as a separate small loop
            int base = end; // original offset, but we already emitted to 'out' with base=0; following spans should be from 'end'
            // For simplicity, mark rest as Default
            if (n>0) push(out, base, base + n, TokenKind::Default);
            return state;
        }
    }

    int i = 0;
    // Detect comment start '#', ignoring inside strings
    while (i < n) {
        char c = s[i];
        if (c==' '||c=='\t') { int j=i+1; while (j<n && (s[j]==' '||s[j]=='\t')) ++j; push(out,i,j,TokenKind::Whitespace); i=j; continue; }
        if (c=='#') { push(out,i,n,TokenKind::Comment); break; }
        // Strings: triple quotes and single-line
        if (c=='"' || c=='\'') {
            char q=c;
            // triple?
            if (i+2 < n && s[i+1]==q && s[i+2]==q) {
                std::string delim(3, q);
                int j = i+3; // search for closing triple
                auto pos = s.find(delim, static_cast<std::size_t>(j));
                if (pos == std::string::npos) {
                    push(out,i,n,TokenKind::String);
                    state.in_raw_string = true; state.raw_delim = delim; return state;
                } else {
                    int end = static_cast<int>(pos + 3);
                    push(out,i,end,TokenKind::String); i=end; continue;
                }
            } else {
                int j=i+1; bool esc=false; while (j<n) { char d=s[j++]; if (esc){esc=false; continue;} if (d=='\\'){esc=true; continue;} if (d==q) break; }
                push(out,i,j,TokenKind::String); i=j; continue;
            }
        }
        if (std::isdigit(static_cast<unsigned char>(c))) { int j=i+1; while (j<n && (std::isalnum(static_cast<unsigned char>(s[j]))||s[j]=='.'||s[j]=='_' )) ++j; push(out,i,j,TokenKind::Number); i=j; continue; }
        if (is_ident_start(c)) { int j=i+1; while (j<n && is_ident_char(s[j])) ++j; std::string id=s.substr(i,j-i); TokenKind k=TokenKind::Identifier; if (kws_.count(id)) k=TokenKind::Keyword; push(out,i,j,k); i=j; continue; }
        if (std::ispunct(static_cast<unsigned char>(c))) { TokenKind k=TokenKind::Operator; if (c==':'||c==','||c=='('||c==')'||c=='['||c==']') k=TokenKind::Punctuation; push(out,i,i+1,k); ++i; continue; }
        push(out,i,i+1,TokenKind::Default); ++i;
    }
    return state;
}

} // namespace kte
