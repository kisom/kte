#include "Test.h"
#include "Buffer.h"
#include <algorithm>
#include <limits>
#include <string>
#include <vector>


static std::vector<std::string>
split_lines_preserve_trailing_empty(const std::string &s)
{
	std::vector<std::string> out;
	std::size_t start = 0;
	for (std::size_t i = 0; i <= s.size(); i++) {
		if (i == s.size() || s[i] == '\n') {
			out.push_back(s.substr(start, i - start));
			start = i + 1;
		}
	}
	if (out.empty())
		out.push_back(std::string());
	return out;
}


static std::vector<std::size_t>
line_starts_for(const std::string &s)
{
	std::vector<std::size_t> starts;
	starts.push_back(0);
	for (std::size_t i = 0; i < s.size(); i++) {
		if (s[i] == '\n')
			starts.push_back(i + 1);
	}
	return starts;
}


static std::size_t
ref_linecol_to_offset(const std::string &s, std::size_t row, std::size_t col)
{
	auto starts = line_starts_for(s);
	if (starts.empty())
		return 0;
	if (row >= starts.size())
		return s.size();
	std::size_t start = starts[row];
	std::size_t end   = (row + 1 < starts.size()) ? starts[row + 1] : s.size();
	if (end > start && s[end - 1] == '\n')
		end -= 1; // clamp before trailing newline
	return start + std::min(col, end - start);
}


static void
check_buffer_matches_model(const Buffer &b, const std::string &model)
{
	auto expected_lines = split_lines_preserve_trailing_empty(model);
	const auto &rows    = b.Rows();
	ASSERT_EQ(rows.size(), expected_lines.size());
	ASSERT_EQ(b.Nrows(), rows.size());

	auto starts = line_starts_for(model);
	ASSERT_EQ(starts.size(), expected_lines.size());

	std::string via_views;
	for (std::size_t i = 0; i < rows.size(); i++) {
		ASSERT_EQ(std::string(rows[i]), expected_lines[i]);
		ASSERT_EQ(b.GetLineString(i), expected_lines[i]);

		std::size_t exp_start = starts[i];
		std::size_t exp_end   = (i + 1 < starts.size()) ? starts[i + 1] : model.size();
		auto r                = b.GetLineRange(i);
		ASSERT_EQ(r.first, exp_start);
		ASSERT_EQ(r.second, exp_end);

		auto v = b.GetLineView(i);
		ASSERT_EQ(std::string(v), model.substr(exp_start, exp_end - exp_start));
		via_views.append(v.data(), v.size());
	}
	ASSERT_EQ(via_views, model);
}


TEST (Buffer_RowsCache_MultiLineEdits_StayConsistent)
{
	Buffer b;
	std::string model;

	check_buffer_matches_model(b, model);

	// Insert text and newlines in a few different ways.
	b.insert_text(0, 0, std::string("abc"));
	model.insert(0, "abc");
	check_buffer_matches_model(b, model);

	b.split_line(0, 1); // a\nbc
	model.insert(ref_linecol_to_offset(model, 0, 1), "\n");
	check_buffer_matches_model(b, model);

	b.insert_text(1, 2, std::string("X")); // a\nbcX
	model.insert(ref_linecol_to_offset(model, 1, 2), "X");
	check_buffer_matches_model(b, model);

	b.join_lines(0); // abcX
	{
		std::size_t off = ref_linecol_to_offset(model, 0, std::numeric_limits<std::size_t>::max());
		if (off < model.size() && model[off] == '\n')
			model.erase(off, 1);
	}
	check_buffer_matches_model(b, model);

	// Insert a multi-line segment in one shot.
	b.insert_text(0, 2, std::string("\n123\nxyz"));
	model.insert(ref_linecol_to_offset(model, 0, 2), "\n123\nxyz");
	check_buffer_matches_model(b, model);

	// Delete spanning across a newline.
	b.delete_text(0, 1, 5);
	{
		std::size_t start  = ref_linecol_to_offset(model, 0, 1);
		std::size_t actual = std::min<std::size_t>(5, model.size() - start);
		model.erase(start, actual);
	}
	check_buffer_matches_model(b, model);

	// Insert/delete whole rows.
	b.insert_row(1, std::string_view("ROW"));
	model.insert(ref_linecol_to_offset(model, 1, 0), "ROW\n");
	check_buffer_matches_model(b, model);

	b.delete_row(1);
	{
		auto starts = line_starts_for(model);
		if (1 < (int) starts.size()) {
			std::size_t start = starts[1];
			std::size_t end   = (2 < starts.size()) ? starts[2] : model.size();
			model.erase(start, end - start);
		}
	}
	check_buffer_matches_model(b, model);
}