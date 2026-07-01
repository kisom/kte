// test_syntax_highlighting.cc - tokenization correctness for LanguageHighlighter
// implementations, focused on multi-line state propagation (block comments,
// triple-quoted strings) that stateless-looking per-line scans easily get wrong.
#include "Test.h"
#include "Buffer.h"
#include "Highlight.h"
#include "syntax/LanguageHighlighter.h"
#include "syntax/GoHighlighter.h"
#include "syntax/RustHighlighter.h"
#include "syntax/SqlHighlighter.h"
#include "syntax/PythonHighlighter.h"

#include <string>
#include <vector>

using kte::HighlightSpan;
using kte::StatefulHighlighter;
using kte::TokenKind;

namespace {
bool
has_kind(const std::vector<HighlightSpan> &spans, TokenKind k)
{
	for (const auto &sp: spans) {
		if (sp.kind == k)
			return true;
	}
	return false;
}
} // namespace


TEST (Syntax_Go_MultiLineBlockComment_PropagatesAcrossLines)
{
	Buffer b;
	b.replace_all_bytes("/* start\nmiddle line\nend */\nfunc foo() {}\n");

	kte::GoHighlighter hl;
	StatefulHighlighter::LineState state;
	std::vector<HighlightSpan> spans;

	// Line 0 opens an unclosed block comment; state must carry forward.
	state = hl.HighlightLineStateful(b, 0, state, spans);
	ASSERT_TRUE(state.in_block_comment);

	// Line 1 is entirely inside the comment; the whole line must be Comment,
	// not re-tokenized as code (this is the bug: without state propagation
	// "middle" and "line" would come back as identifiers).
	spans.clear();
	state = hl.HighlightLineStateful(b, 1, state, spans);
	ASSERT_TRUE(state.in_block_comment);
	ASSERT_TRUE(!spans.empty());
	for (const auto &sp: spans) {
		ASSERT_TRUE(sp.kind == TokenKind::Comment);
	}

	// Line 2 closes the comment.
	spans.clear();
	state = hl.HighlightLineStateful(b, 2, state, spans);
	ASSERT_TRUE(!state.in_block_comment);
	ASSERT_TRUE(has_kind(spans, TokenKind::Comment));

	// Line 3 is ordinary code again: "func" must be a Keyword, not a Comment.
	spans.clear();
	state = hl.HighlightLineStateful(b, 3, state, spans);
	ASSERT_TRUE(!state.in_block_comment);
	ASSERT_TRUE(has_kind(spans, TokenKind::Keyword));
	ASSERT_TRUE(!has_kind(spans, TokenKind::Comment));
}


TEST (Syntax_Rust_MultiLineBlockComment_PropagatesAcrossLines)
{
	Buffer b;
	b.replace_all_bytes("/* start\nmiddle line\nend */\nfn foo() {}\n");

	kte::RustHighlighter hl;
	StatefulHighlighter::LineState state;
	std::vector<HighlightSpan> spans;

	state = hl.HighlightLineStateful(b, 0, state, spans);
	ASSERT_TRUE(state.in_block_comment);

	spans.clear();
	state = hl.HighlightLineStateful(b, 1, state, spans);
	ASSERT_TRUE(state.in_block_comment);
	for (const auto &sp: spans) {
		ASSERT_TRUE(sp.kind == TokenKind::Comment);
	}

	spans.clear();
	state = hl.HighlightLineStateful(b, 2, state, spans);
	ASSERT_TRUE(!state.in_block_comment);

	spans.clear();
	state = hl.HighlightLineStateful(b, 3, state, spans);
	ASSERT_TRUE(has_kind(spans, TokenKind::Keyword));
	ASSERT_TRUE(!has_kind(spans, TokenKind::Comment));
}


TEST (Syntax_Sql_MultiLineBlockComment_PropagatesAcrossLines)
{
	Buffer b;
	b.replace_all_bytes("/* start\nmiddle line\nend */\nSELECT 1;\n");

	kte::SqlHighlighter hl;
	StatefulHighlighter::LineState state;
	std::vector<HighlightSpan> spans;

	state = hl.HighlightLineStateful(b, 0, state, spans);
	ASSERT_TRUE(state.in_block_comment);

	spans.clear();
	state = hl.HighlightLineStateful(b, 1, state, spans);
	ASSERT_TRUE(state.in_block_comment);
	for (const auto &sp: spans) {
		ASSERT_TRUE(sp.kind == TokenKind::Comment);
	}

	spans.clear();
	state = hl.HighlightLineStateful(b, 2, state, spans);
	ASSERT_TRUE(!state.in_block_comment);

	spans.clear();
	state = hl.HighlightLineStateful(b, 3, state, spans);
	ASSERT_TRUE(has_kind(spans, TokenKind::Keyword));
	ASSERT_TRUE(!has_kind(spans, TokenKind::Comment));
}


TEST (Syntax_Python_TripleQuote_ClosesAndReopensOnSameLine)
{
	Buffer b;
	// Line 0 opens a triple-quoted string that stays open.
	// Line 1 closes the first string ("end'''") and, on the SAME line, opens a
	// second triple-quoted string ("'''start of") that stays open into line 2.
	b.replace_all_bytes("x = '''abc\nend''' + '''start of\nnext string'''\n");

	kte::PythonHighlighter hl;
	StatefulHighlighter::LineState state;
	std::vector<HighlightSpan> spans;

	state = hl.HighlightLineStateful(b, 0, state, spans);
	ASSERT_TRUE(state.in_raw_string);
	ASSERT_EQ(state.raw_delim, std::string("'''"));

	spans.clear();
	state = hl.HighlightLineStateful(b, 1, state, spans);
	// The bug: without re-scanning the remainder after the closing '''  the
	// second opening ''' on this line is never noticed, so state.in_raw_string
	// would incorrectly come back false here.
	ASSERT_TRUE(state.in_raw_string);
	ASSERT_EQ(state.raw_delim, std::string("'''"));

	// Line 2 is inside the second string; it must be highlighted as String,
	// not as ordinary code.
	spans.clear();
	state = hl.HighlightLineStateful(b, 2, state, spans);
	ASSERT_TRUE(!spans.empty());
	bool all_string = true;
	for (const auto &sp: spans) {
		if (sp.kind != TokenKind::String)
			all_string = false;
	}
	ASSERT_TRUE(all_string);
}
