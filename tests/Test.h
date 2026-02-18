// Minimal header-only unit test framework for kte
#pragma once
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>

namespace ktet {
struct TestCase {
	std::string name;
	std::function<void()> fn;
};


inline std::vector<TestCase> &
registry()
{
	static std::vector<TestCase> r;
	return r;
}


struct Registrar {
	Registrar(const char *name, std::function<void()> fn)
	{
		registry().push_back(TestCase{std::string(name), std::move(fn)});
	}
};

// Assertions
struct AssertionFailure {
	std::string msg;
};


inline void
expect(bool cond, const char *expr, const char *file, int line)
{
	if (!cond) {
		std::cerr << file << ":" << line << ": EXPECT failed: " << expr << "\n";
	}
}


inline void
assert_true(bool cond, const char *expr, const char *file, int line)
{
	if (!cond) {
		throw AssertionFailure{std::string(file) + ":" + std::to_string(line) + ": ASSERT failed: " + expr};
	}
}


template<typename A, typename B>
inline void
assert_eq_impl(const A &a, const B &b, const char *ea, const char *eb, const char *file, int line)
{
	// Cast to common type to avoid signed/unsigned comparison warnings
	using Common = std::common_type_t<A, B>;
	if (!(static_cast<Common>(a) == static_cast<Common>(b))) {
		std::ostringstream oss;
		oss << file << ":" << line << ": ASSERT_EQ failed: " << ea << " == " << eb;
		throw AssertionFailure{oss.str()};
	}
}
} // namespace ktet

#define TEST(name) \
    static void name(); \
    static ::ktet::Registrar _reg_##name(#name, &name); \
    static void name()

#define EXPECT_TRUE(x) ::ktet::expect((x), #x, __FILE__, __LINE__)
#define ASSERT_TRUE(x) ::ktet::assert_true((x), #x, __FILE__, __LINE__)
#define ASSERT_EQ(a,b) ::ktet::assert_eq_impl((a),(b), #a, #b, __FILE__, __LINE__)