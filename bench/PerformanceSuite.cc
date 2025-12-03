/*
 * PerformanceSuite.cc - broader performance and verification benchmarks
 */

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <typeinfo>
#include <vector>

#include "GapBuffer.h"
#include "PieceTable.h"
#include "OptimizedSearch.h"

using clock_t = std::chrono::steady_clock;
using us      = std::chrono::microseconds;

namespace {
struct Stat {
	double micros{0.0};
	std::size_t bytes{0};
	std::size_t ops{0};
};


static void
print_header(const std::string &title)
{
	std::cout << "\n" << title << "\n";
	std::cout << std::left << std::setw(18) << "Case"
		<< std::left << std::setw(18) << "Type"
		<< std::right << std::setw(12) << "time(us)"
		<< std::right << std::setw(14) << "bytes"
		<< std::right << std::setw(14) << "ops/s"
		<< std::right << std::setw(14) << "MB/s"
		<< "\n";
	std::cout << std::string(90, '-') << "\n";
}


static void
print_row(const std::string &caseName, const std::string &typeName, const Stat &s)
{
	double mb   = s.bytes / (1024.0 * 1024.0);
	double sec  = s.micros / 1'000'000.0;
	double mbps = sec > 0 ? (mb / sec) : 0.0;
	double opss = sec > 0 ? (static_cast<double>(s.ops) / sec) : 0.0;
	std::cout << std::left << std::setw(18) << caseName
		<< std::left << std::setw(18) << typeName
		<< std::right << std::setw(12) << std::fixed << std::setprecision(2) << s.micros
		<< std::right << std::setw(14) << s.bytes
		<< std::right << std::setw(14) << std::fixed << std::setprecision(2) << opss
		<< std::right << std::setw(14) << std::fixed << std::setprecision(2) << mbps
		<< "\n";
}
} // namespace

class PerformanceSuite {
public:
	void benchmarkBufferOperations(std::size_t N, int rounds, std::size_t chunk)
	{
		print_header("Buffer Operations");
		run_buffer_case<GapBuffer>("append_char", N, rounds, chunk, [&](auto &b, std::size_t count) {
			for (std::size_t i = 0; i < count; ++i)
				b.AppendChar('a');
		});
		run_buffer_case<GapBuffer>("prepend_char", N, rounds, chunk, [&](auto &b, std::size_t count) {
			for (std::size_t i = 0; i < count; ++i)
				b.PrependChar('a');
		});
		run_buffer_case<GapBuffer>("chunk_mix", N, rounds, chunk, [&](auto &b, std::size_t) {
			std::string payload(chunk, 'x');
			std::size_t written = 0;
			while (written < N) {
				std::size_t now = std::min(chunk, N - written);
				if (((written / chunk) & 1) == 0)
					b.Append(payload.data(), now);
				else
					b.Prepend(payload.data(), now);
				written += now;
			}
		});
		run_buffer_case<PieceTable>("append_char", N, rounds, chunk, [&](auto &b, std::size_t count) {
			for (std::size_t i = 0; i < count; ++i)
				b.AppendChar('a');
		});
		run_buffer_case<PieceTable>("prepend_char", N, rounds, chunk, [&](auto &b, std::size_t count) {
			for (std::size_t i = 0; i < count; ++i)
				b.PrependChar('a');
		});
		run_buffer_case<PieceTable>("chunk_mix", N, rounds, chunk, [&](auto &b, std::size_t) {
			std::string payload(chunk, 'x');
			std::size_t written = 0;
			while (written < N) {
				std::size_t now = std::min(chunk, N - written);
				if (((written / chunk) & 1) == 0)
					b.Append(payload.data(), now);
				else
					b.Prepend(payload.data(), now);
				written += now;
			}
		});
	}


	void benchmarkSearchOperations(std::size_t textLen, std::size_t patLen, int rounds)
	{
		print_header("Search Operations");
		std::mt19937_64 rng(0xC0FFEE);
		std::uniform_int_distribution<int> dist('a', 'z');
		std::string text(textLen, '\0');
		for (auto &ch: text)
			ch = static_cast<char>(dist(rng));
		std::string pattern(patLen, '\0');
		for (auto &ch: pattern)
			ch = static_cast<char>(dist(rng));

		// Ensure at least one hit
		if (textLen >= patLen && patLen > 0) {
			std::size_t pos = textLen / 2;
			std::memcpy(&text[pos], pattern.data(), patLen);
		}

		// OptimizedSearch find_all vs std::string reference
		OptimizedSearch os;
		Stat s{};
		auto start               = clock_t::now();
		std::size_t matches      = 0;
		std::size_t bytesScanned = 0;
		for (int r = 0; r < rounds; ++r) {
			auto hits = os.find_all(text, pattern, 0);
			matches += hits.size();
			bytesScanned += text.size();
			// Verify with reference
			std::vector<std::size_t> ref;
			std::size_t from = 0;
			while (true) {
				auto p = text.find(pattern, from);
				if (p == std::string::npos)
					break;
				ref.push_back(p);
				from = p + (patLen ? patLen : 1);
			}
			assert(ref == hits);
		}
		auto end = clock_t::now();
		s.micros = std::chrono::duration_cast<us>(end - start).count();
		s.bytes  = bytesScanned;
		s.ops    = matches;
		print_row("find_all", "OptimizedSearch", s);
	}


	void benchmarkMemoryAllocation(std::size_t N, int rounds)
	{
		print_header("Memory Allocation (allocations during editing)");
		// Measure number of allocations by simulating editing patterns.
		auto run_session = [&](auto &&buffer) {
			// alternate small appends and prepends
			const std::size_t chunk = 32;
			std::string payload(chunk, 'q');
			for (int r = 0; r < rounds; ++r) {
				buffer.Clear();
				for (std::size_t i = 0; i < N; i += chunk)
					buffer.Append(payload.data(), std::min(chunk, N - i));
				for (std::size_t i = 0; i < N / 2; i += chunk)
					buffer.Prepend(payload.data(), std::min(chunk, N / 2 - i));
			}
		};

		// Local allocation counters for this TU via overriding operators
		reset_alloc_counters();
		GapBuffer gb;
		run_session(gb);
		auto gap_allocs = current_allocs();
		print_row("edit_session", "GapBuffer", Stat{
			          0.0, static_cast<std::size_t>(gap_allocs.bytes),
			          static_cast<std::size_t>(gap_allocs.count)
		          });

		reset_alloc_counters();
		PieceTable pt;
		run_session(pt);
		auto pt_allocs = current_allocs();
		print_row("edit_session", "PieceTable", Stat{
			          0.0, static_cast<std::size_t>(pt_allocs.bytes),
			          static_cast<std::size_t>(pt_allocs.count)
		          });
	}

private:
	template<typename Buf, typename Fn>
	void run_buffer_case(const std::string &caseName, std::size_t N, int rounds, std::size_t chunk, Fn fn)
	{
		Stat s{};
		auto start        = clock_t::now();
		std::size_t bytes = 0;
		std::size_t ops   = 0;
		for (int t = 0; t < rounds; ++t) {
			Buf b;
			b.Reserve(N);
			fn(b, N);
			// compare to reference string where possible (only for append_char/prepend_char)
			bytes += N;
			ops += N / (chunk ? chunk : 1);
		}
		auto end = clock_t::now();
		s.micros = std::chrono::duration_cast<us>(end - start).count();
		s.bytes  = bytes;
		s.ops    = ops;
		print_row(caseName, typeid(Buf).name(), s);
	}


	// Simple global allocation tracking for this TU
	struct AllocStats {
		std::uint64_t count{0};
		std::uint64_t bytes{0};
	};


	static AllocStats &alloc_stats()
	{
		static AllocStats s;
		return s;
	}


	static void reset_alloc_counters()
	{
		alloc_stats() = {};
	}


	static AllocStats current_allocs()
	{
		return alloc_stats();
	}


	// Friend global new/delete defined below
	friend void *operator new(std::size_t sz) noexcept(false);

	friend void operator delete(void *p) noexcept;

	friend void *operator new[](std::size_t sz) noexcept(false);

	friend void operator delete[](void *p) noexcept;
};

// Override new/delete only in this translation unit to track allocations made here
void *
operator new(std::size_t sz) noexcept(false)
{
	auto &s = PerformanceSuite::alloc_stats();
	s.count++;
	s.bytes += sz;
	if (void *p = std::malloc(sz))
		return p;
	throw std::bad_alloc();
}


void
operator delete(void *p) noexcept
{
	std::free(p);
}


void *
operator new[](std::size_t sz) noexcept(false)
{
	auto &s = PerformanceSuite::alloc_stats();
	s.count++;
	s.bytes += sz;
	if (void *p = std::malloc(sz))
		return p;
	throw std::bad_alloc();
}


void
operator delete[](void *p) noexcept
{
	std::free(p);
}


int
main(int argc, char **argv)
{
	std::size_t N     = 200'000; // bytes per round for buffer cases
	int rounds        = 3;
	std::size_t chunk = 1024;
	if (argc >= 2)
		N = static_cast<std::size_t>(std::stoull(argv[1]));
	if (argc >= 3)
		rounds = std::stoi(argv[2]);
	if (argc >= 4)
		chunk = static_cast<std::size_t>(std::stoull(argv[3]));

	std::cout << "KTE Performance Suite" << "\n";
	std::cout << "N=" << N << ", rounds=" << rounds << ", chunk=" << chunk << "\n";

	PerformanceSuite suite;
	suite.benchmarkBufferOperations(N, rounds, chunk);
	suite.benchmarkSearchOperations(1'000'000, 16, rounds);
	suite.benchmarkMemoryAllocation(N, rounds);
	return 0;
}