// Verify OptimizedSearch against std::string reference across patterns and sizes
#include <cassert>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "OptimizedSearch.h"


static std::vector<std::size_t>
ref_find_all(const std::string &text, const std::string &pat)
{
	std::vector<std::size_t> res;
	if (pat.empty())
		return res;
	std::size_t from = 0;
	while (true) {
		auto p = text.find(pat, from);
		if (p == std::string::npos)
			break;
		res.push_back(p);
		from = p + pat.size(); // non-overlapping
	}
	return res;
}


static void
run_case(std::size_t textLen, std::size_t patLen, unsigned seed)
{
	std::mt19937 rng(seed);
	std::uniform_int_distribution<int> dist('a', 'z');
	std::string text(textLen, '\0');
	for (auto &ch: text)
		ch = static_cast<char>(dist(rng));
	std::string pat(patLen, '\0');
	for (auto &ch: pat)
		ch = static_cast<char>(dist(rng));

	// Guarantee at least one match when possible
	if (textLen >= patLen && patLen > 0) {
		std::size_t pos = textLen / 3;
		if (pos + patLen <= text.size())
			std::copy(pat.begin(), pat.end(), text.begin() + static_cast<long>(pos));
	}

	OptimizedSearch os;
	auto got = os.find_all(text, pat, 0);
	auto ref = ref_find_all(text, pat);
	assert(got == ref);
}


int
main()
{
	// Edge cases
	run_case(0, 0, 1);
	run_case(0, 1, 2);
	run_case(1, 0, 3);
	run_case(1, 1, 4);

	// Various sizes
	for (std::size_t t = 128; t <= 4096; t *= 2) {
		for (std::size_t p = 1; p <= 64; p *= 2) {
			run_case(t, p, static_cast<unsigned>(t + p));
		}
	}
	// Larger random
	run_case(100000, 16, 12345);
	run_case(250000, 32, 67890);
	return 0;
}