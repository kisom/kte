// OptimizedSearch.h - Boyer–Moore (bad character) based substring search
#ifndef KTE_OPTIMIZED_SEARCH_H
#define KTE_OPTIMIZED_SEARCH_H

#include <array>
#include <cstddef>
#include <string>
#include <vector>

class OptimizedSearch {
public:
	OptimizedSearch() = default;

	// Find first occurrence at or after start. Returns npos if not found.
	std::size_t find_first(const std::string &text, const std::string &pattern, std::size_t start = 0);

	// Find all non-overlapping matches at or after start. Returns starting indices.
	std::vector<std::size_t> find_all(const std::string &text, const std::string &pattern, std::size_t start = 0);

private:
	std::array<int, 256> bad_char_{};
	std::string last_pat_;

	void build_bad_char(const std::string &pattern);
};

#endif // KTE_OPTIMIZED_SEARCH_H