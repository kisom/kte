#include "RustHighlighter.h"
#include "Buffer.h"
#include <cctype>

namespace kte {

static void push(std::vector<HighlightSpan> &out, int a, int b, TokenKind k){ if (b>a) out.push_back({a,b,k}); }
static bool is_ident_start(char c){ return std::isalpha(static_cast<unsigned char>(c)) || c=='_'; }
static bool is_ident_char(char c){ return std::isalnum(static_cast<unsigned char>(c)) || c=='_'; }

RustHighlighter::RustHighlighter()
{
    const char* kw[] = {"as","break","const","continue","crate","else","enum","extern","false","fn","for","if","impl","in","let","loop","match","mod","move","mut","pub","ref","return","self","Self","static","struct","super","trait","true","type","unsafe","use","where","while","dyn","async","await","try"};
    for (auto s: kw) kws_.insert(s);
    const char* tp[] = {"u8","u16","u32","u64","u128","usize","i8","i16","i32","i64","i128","isize","f32","f64","bool","char","str"};
    for (auto s: tp) types_.insert(s);
}

void RustHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
    const auto &rows = buf.Rows();
    if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) return;
    std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
    int n = static_cast<int>(s.size());
    int i = 0;
    while (i < n) {
        char c = s[i];
        if (c==' '||c=='\t') { int j=i+1; while (j<n && (s[j]==' '||s[j]=='\t')) ++j; push(out,i,j,TokenKind::Whitespace); i=j; continue; }
        if (c=='/' && i+1<n && s[i+1]=='/') { push(out,i,n,TokenKind::Comment); break; }
        if (c=='/' && i+1<n && s[i+1]=='*') { int j=i+2; bool closed=false; while (j+1<=n) { if (j+1<n && s[j]=='*' && s[j+1]=='/') { j+=2; closed=true; break; } ++j; } if (!closed) { push(out,i,n,TokenKind::Comment); break; } else { push(out,i,j,TokenKind::Comment); i=j; continue; } }
        if (c=='"') { int j=i+1; bool esc=false; while (j<n){ char d=s[j++]; if (esc){esc=false; continue;} if (d=='\\'){esc=true; continue;} if (d=='"') break; } push(out,i,j,TokenKind::String); i=j; continue; }
        if (std::isdigit(static_cast<unsigned char>(c))) { int j=i+1; while (j<n && (std::isalnum(static_cast<unsigned char>(s[j]))||s[j]=='.'||s[j]=='_' )) ++j; push(out,i,j,TokenKind::Number); i=j; continue; }
        if (is_ident_start(c)) { int j=i+1; while (j<n && is_ident_char(s[j])) ++j; std::string id=s.substr(i,j-i); TokenKind k=TokenKind::Identifier; if (kws_.count(id)) k=TokenKind::Keyword; else if (types_.count(id)) k=TokenKind::Type; push(out,i,j,k); i=j; continue; }
        if (std::ispunct(static_cast<unsigned char>(c))) { TokenKind k=TokenKind::Operator; if (c==';'||c==','||c=='('||c==')'||c=='{'||c=='}'||c=='['||c==']') k=TokenKind::Punctuation; push(out,i,i+1,k); ++i; continue; }
        push(out,i,i+1,TokenKind::Default); ++i;
    }
}

} // namespace kte
