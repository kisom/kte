#include "LispHighlighter.h"
#include "Buffer.h"
#include <cctype>

namespace kte {

static void push(std::vector<HighlightSpan> &out, int a, int b, TokenKind k){ if (b>a) out.push_back({a,b,k}); }

LispHighlighter::LispHighlighter()
{
    const char* kw[] = {"defun","lambda","let","let*","define","set!","if","cond","begin","quote","quasiquote","unquote","unquote-splicing","loop","do","and","or","not"};
    for (auto s: kw) kws_.insert(s);
}

void LispHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
    const auto &rows = buf.Rows();
    if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) return;
    std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
    int n = static_cast<int>(s.size());
    int i = 0;
    int bol = 0; while (bol<n && (s[bol]==' '||s[bol]=='\t')) ++bol;
    if (bol < n && s[bol] == ';') { push(out, bol, n, TokenKind::Comment); if (bol>0) push(out,0,bol,TokenKind::Whitespace); return; }
    while (i < n) {
        char c = s[i];
        if (c==' '||c=='\t') { int j=i+1; while (j<n && (s[j]==' '||s[j]=='\t')) ++j; push(out,i,j,TokenKind::Whitespace); i=j; continue; }
        if (c==';') { push(out,i,n,TokenKind::Comment); break; }
        if (c=='"') { int j=i+1; bool esc=false; while (j<n){ char d=s[j++]; if (esc){esc=false; continue;} if (d=='\\'){esc=true; continue;} if (d=='"') break; } push(out,i,j,TokenKind::String); i=j; continue; }
        if (std::isalpha(static_cast<unsigned char>(c)) || c=='*' || c=='-' || c=='+' || c=='/' || c=='_' ) {
            int j=i+1; while (j<n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j]=='*' || s[j]=='-' || s[j]=='+' || s[j]=='/' || s[j]=='_' || s[j]=='!')) ++j;
            std::string id=s.substr(i,j-i);
            TokenKind k = kws_.count(id) ? TokenKind::Keyword : TokenKind::Identifier;
            push(out,i,j,k); i=j; continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) { int j=i+1; while (j<n && (std::isdigit(static_cast<unsigned char>(s[j]))||s[j]=='.')) ++j; push(out,i,j,TokenKind::Number); i=j; continue; }
        if (std::ispunct(static_cast<unsigned char>(c))) { TokenKind k=TokenKind::Punctuation; push(out,i,i+1,k); ++i; continue; }
        push(out,i,i+1,TokenKind::Default); ++i;
    }
}

} // namespace kte
