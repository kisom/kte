#include "Test.h"
#include <iostream>
#include <chrono>
#include <string>
#include <vector>


// Usage: kte_tests [substring...] runs the tests whose names contain any of
// the substrings (all tests when none are given).
int
main(int argc, char **argv)
{
	using namespace std::chrono;
	auto &reg = ktet::registry();
	std::vector<std::string> filters(argv + 1, argv + argc);
	auto selected = [&filters](const std::string &name) {
		if (filters.empty())
			return true;
		for (const auto &f: filters)
			if (name.find(f) != std::string::npos)
				return true;
		return false;
	};
	std::cout << "kte unit tests: " << reg.size() << " test(s)\n";
	int failed = 0;
	auto t0    = steady_clock::now();
	for (const auto &tc: reg) {
		if (!selected(tc.name))
			continue;
		auto ts = steady_clock::now();
		try {
			tc.fn();
			auto te = steady_clock::now();
			auto ms = duration_cast<milliseconds>(te - ts).count();
			std::cout << "[ OK ] " << tc.name << " (" << ms << " ms)\n";
		} catch (const ktet::AssertionFailure &e) {
			++failed;
			std::cerr << "[FAIL] " << tc.name << " -> " << e.msg << "\n";
		} catch (const std::exception &e) {
			++failed;
			std::cerr << "[EXCP] " << tc.name << " -> " << e.what() << "\n";
		} catch (...) {
			++failed;
			std::cerr << "[EXCP] " << tc.name << " -> unknown exception\n";
		}
	}
	auto t1       = steady_clock::now();
	auto total_ms = duration_cast<milliseconds>(t1 - t0).count();
	std::cout << "Done in " << total_ms << " ms. Failures: " << failed << "\n";
	return failed == 0 ? 0 : 1;
}
