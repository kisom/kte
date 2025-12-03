#include "OptimizedSearch.h"

#include <algorithm>


void
OptimizedSearch::build_bad_char(const std::string &pattern)
{
	if (pattern == last_pat_)
		return;
	last_pat_ = pattern;
	std::fill(bad_char_.begin(), bad_char_.end(), -1);
	for (std::size_t i = 0; i < pattern.size(); ++i) {
		bad_char_[static_cast<unsigned char>(pattern[i])] = static_cast<int>(i);
	}
}


std::size_t
OptimizedSearch::find_first(const std::string &text, const std::string &pattern, std::size_t start)
{
	const std::size_t n = text.size();
	const std::size_t m = pattern.size();
	if (m == 0)
		return start <= n ? start : std::string::npos;
	if (m > n || start >= n)
		return std::string::npos;
	build_bad_char(pattern);
	std::size_t s = start;
	while (s <= n - m) {
		std::size_t j = m;
		while (j > 0 && pattern[j - 1] == text[s + j - 1]) {
			--j;
		}
		if (j == 0) {
			return s; // match found
		}
		unsigned char badc = static_cast<unsigned char>(text[s + j - 1]);
		int bcidx          = bad_char_[badc];
		std::size_t shift  = (j - 1 > static_cast<std::size_t>(bcidx))
			                    ? (j - 1 - static_cast<std::size_t>(bcidx))
			                    : 1;
		s += shift;
	}
	return std::string::npos;
}


std::vector<std::size_t>
OptimizedSearch::find_all(const std::string &text, const std::string &pattern, std::size_t start)
{
	std::vector<std::size_t> res;
	const std::size_t n = text.size();
	const std::size_t m = pattern.size();
	if (m == 0)
		return res;
	if (m > n || start >= n)
		return res;
	build_bad_char(pattern);
	std::size_t s = start;
	while (s <= n - m) {
		std::size_t j = m;
		while (j > 0 && pattern[j - 1] == text[s + j - 1]) {
			--j;
		}
		if (j == 0) {
			res.push_back(s);
			s += m; // non-overlapping
			continue;
		}
		unsigned char badc = static_cast<unsigned char>(text[s + j - 1]);
		int bcidx          = bad_char_[badc];
		std::size_t shift  = (j - 1 > static_cast<std::size_t>(bcidx))
			                    ? (j - 1 - static_cast<std::size_t>(bcidx))
			                    : 1;
		s += shift;
	}
	return res;
}
