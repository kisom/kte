// Simple buffer correctness tests comparing GapBuffer and PieceTable to std::string
#include <cassert>
#include <cstddef>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "GapBuffer.h"
#include "PieceTable.h"


template<typename Buf>
static void
check_equals(const Buf &b, const std::string &ref)
{
	assert(b.Size() == ref.size());
	if (b.Size() == 0)
		return;
	const char *p = b.Data();
	assert(p != nullptr);
	assert(std::memcmp(p, ref.data(), ref.size()) == 0);
}


template<typename Buf>
static void
run_basic_cases()
{
	// empty
	{
		Buf b;
		std::string ref;
		check_equals(b, ref);
	}

	// append chars
	{
		Buf b;
		std::string ref;
		for (int i = 0; i < 1000; ++i) {
			b.AppendChar('a');
			ref.push_back('a');
		}
		check_equals(b, ref);
	}

	// prepend chars
	{
		Buf b;
		std::string ref;
		for (int i = 0; i < 1000; ++i) {
			b.PrependChar('b');
			ref.insert(ref.begin(), 'b');
		}
		check_equals(b, ref);
	}

	// append/prepend strings
	{
		Buf b;
		std::string ref;
		const char *hello = "hello";
		b.Append(hello, 5);
		ref.append("hello");
		b.Prepend(hello, 5);
		ref.insert(0, "hello");
		check_equals(b, ref);
	}

	// larger random blocks
	{
		std::mt19937 rng(42);
		std::uniform_int_distribution<int> len_dist(0, 128);
		std::uniform_int_distribution<int> coin(0, 1);
		Buf b;
		std::string ref;
		for (int step = 0; step < 2000; ++step) {
			int L = len_dist(rng);
			std::string payload(L, '\0');
			for (int i         = 0; i < L; ++i)
				payload[i] = static_cast<char>('a' + (i % 26));
			if (coin(rng)) {
				b.Append(payload.data(), payload.size());
				ref.append(payload);
			} else {
				b.Prepend(payload.data(), payload.size());
				ref.insert(0, payload);
			}
		}
		check_equals(b, ref);
	}
}


int
main()
{
	run_basic_cases<GapBuffer>();
	run_basic_cases<PieceTable>();
	return 0;
}