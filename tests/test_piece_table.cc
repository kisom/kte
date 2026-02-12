#include "Test.h"
#include "PieceTable.h"
#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <vector>


static std::vector<std::size_t>
LineStartsFor(const std::string &s)
{
	std::vector<std::size_t> starts;
	starts.push_back(0);
	for (std::size_t i = 0; i < s.size(); i++) {
		if (s[i] == '\n')
			starts.push_back(i + 1);
	}
	return starts;
}


static std::string
LineContentFor(const std::string &s, std::size_t line_num)
{
	auto starts = LineStartsFor(s);
	if (starts.empty() || line_num >= starts.size())
		return std::string();
	std::size_t start = starts[line_num];
	std::size_t end   = (line_num + 1 < starts.size()) ? starts[line_num + 1] : s.size();
	if (end > start && s[end - 1] == '\n')
		end -= 1;
	return s.substr(start, end - start);
}


TEST (PieceTable_Insert_Delete_LineCount)
{
	PieceTable pt;
	// start empty
	ASSERT_EQ(pt.Size(), (std::size_t) 0);
	ASSERT_EQ(pt.LineCount(), (std::size_t) 1); // empty buffer has 1 logical line

	// Insert some text with newlines
	const char *t = "abc\n123\nxyz"; // last line without trailing NL
	pt.Insert(0, t, 11);
	ASSERT_EQ(pt.Size(), (std::size_t) 11);
	ASSERT_EQ(pt.LineCount(), (std::size_t) 3);

	// Check get line
	ASSERT_EQ(pt.GetLine(0), std::string("abc"));
	ASSERT_EQ(pt.GetLine(1), std::string("123"));
	ASSERT_EQ(pt.GetLine(2), std::string("xyz"));

	// Delete middle line entirely including its trailing NL
	auto r = pt.GetLineRange(1); // [start,end) points to start of line 1 to start of line 2
	pt.Delete(r.first, r.second - r.first);
	ASSERT_EQ(pt.LineCount(), (std::size_t) 2);
	ASSERT_EQ(pt.GetLine(0), std::string("abc"));
	ASSERT_EQ(pt.GetLine(1), std::string("xyz"));
}


TEST (PieceTable_LineCol_Conversions)
{
	PieceTable pt;
	std::string s = "hello\nworld\n"; // two lines with trailing NL
	pt.Insert(0, s.data(), s.size());

	// Byte offsets of starts
	auto off0 = pt.LineColToByteOffset(0, 0);
	auto off1 = pt.LineColToByteOffset(1, 0);
	auto off2 = pt.LineColToByteOffset(2, 0); // EOF
	ASSERT_EQ(off0, (std::size_t) 0);
	ASSERT_EQ(off1, (std::size_t) 6); // "hello\n"
	ASSERT_EQ(off2, pt.Size());

	auto lc0 = pt.ByteOffsetToLineCol(0);
	auto lc1 = pt.ByteOffsetToLineCol(6);
	ASSERT_EQ(lc0.first, (std::size_t) 0);
	ASSERT_EQ(lc0.second, (std::size_t) 0);
	ASSERT_EQ(lc1.first, (std::size_t) 1);
	ASSERT_EQ(lc1.second, (std::size_t) 0);
}


TEST (PieceTable_ReferenceModel_RandomEdits_Deterministic)
{
	PieceTable pt;
	std::string model;

	std::mt19937 rng(0xC0FFEEu);
	const std::vector<std::string> corpus = {
		"a",
		"b",
		"c",
		"xyz",
		"123",
		"\n",
		"!\n",
		"foo\nbar",
		"end\n",
	};

	auto check_invariants = [&](const char *where) {
		(void) where;
		ASSERT_EQ(pt.Size(), model.size());
		ASSERT_EQ(pt.GetRange(0, pt.Size()), model);

		auto starts = LineStartsFor(model);
		ASSERT_EQ(pt.LineCount(), starts.size());

		// Spot-check a few line ranges and contents.
		std::size_t last                             = starts.empty() ? (std::size_t) 0 : (starts.size() - 1);
		std::size_t mid                              = (starts.size() > 2) ? (std::size_t) 1 : last;
		const std::array<std::size_t, 3> probe_lines = {(std::size_t) 0, last, mid};
		for (auto line: probe_lines) {
			if (starts.empty())
				break;
			if (line >= starts.size())
				continue;
			std::size_t exp_start = starts[line];
			std::size_t exp_end   = (line + 1 < starts.size()) ? starts[line + 1] : model.size();
			auto r                = pt.GetLineRange(line);
			ASSERT_EQ(r.first, exp_start);
			ASSERT_EQ(r.second, exp_end);
			ASSERT_EQ(pt.GetLine(line), LineContentFor(model, line));
		}

		// Round-trips for a few offsets.
		const std::vector<std::size_t> probe_offsets = {
			0,
			model.size() / 2,
			model.size(),
		};
		for (auto off: probe_offsets) {
			auto lc   = pt.ByteOffsetToLineCol(off);
			auto back = pt.LineColToByteOffset(lc.first, lc.second);
			ASSERT_EQ(back, off);
		}
	};

	check_invariants("initial");

	for (int step = 0; step < 250; step++) {
		bool do_insert = model.empty() || ((rng() % 3u) != 0u); // bias toward insert
		if (do_insert) {
			const std::string &ins = corpus[rng() % corpus.size()];
			std::size_t pos        = model.empty() ? 0 : (rng() % (model.size() + 1));
			pt.Insert(pos, ins.data(), ins.size());
			model.insert(pos, ins);
		} else {
			std::size_t pos = rng() % model.size();
			std::size_t max = std::min<std::size_t>(8, model.size() - pos);
			std::size_t len = 1 + (rng() % max);
			pt.Delete(pos, len);
			model.erase(pos, len);
		}

		// Also validate GetRange on a small random window when non-empty.
		if (!model.empty()) {
			std::size_t off = rng() % model.size();
			std::size_t max = std::min<std::size_t>(16, model.size() - off);
			std::size_t len = 1 + (rng() % max);
			ASSERT_EQ(pt.GetRange(off, len), model.substr(off, len));
		}

		check_invariants("step");
	}

	// Full line-by-line range verification at the end.
	auto starts = LineStartsFor(model);
	for (std::size_t line = 0; line < starts.size(); line++) {
		std::size_t exp_start = starts[line];
		std::size_t exp_end   = (line + 1 < starts.size()) ? starts[line + 1] : model.size();
		auto r                = pt.GetLineRange(line);
		ASSERT_EQ(r.first, exp_start);
		ASSERT_EQ(r.second, exp_end);
		ASSERT_EQ(pt.GetLine(line), LineContentFor(model, line));
	}
}