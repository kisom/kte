/*
 * BufferBench.cc - microbenchmarks for GapBuffer and PieceTable
 *
 * This benchmark exercises the public APIs shared by both structures as used
 * in Buffer::Line: Reserve, AppendChar, Append, PrependChar, Prepend, Clear.
 *
 * Run examples:
 *   ./kte_bench_buffer                  # defaults
 *   ./kte_bench_buffer 200000 8 4096    # N=200k, rounds=8, chunk=4096
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <typeinfo>

#include "GapBuffer.h"
#include "PieceTable.h"

using clock_t = std::chrono::steady_clock;
using us      = std::chrono::microseconds;

struct Result {
	std::string name;
	std::string scenario;
	double micros     = 0.0;
	std::size_t bytes = 0;
};


static void
print_header()
{
	std::cout << std::left << std::setw(14) << "Structure"
		<< std::left << std::setw(18) << "Scenario"
		<< std::right << std::setw(12) << "time(us)"
		<< std::right << std::setw(14) << "bytes"
		<< std::right << std::setw(14) << "MB/s"
		<< "\n";
	std::cout << std::string(72, '-') << "\n";
}


static void
print_row(const Result &r)
{
	double mb   = r.bytes / (1024.0 * 1024.0);
	double mbps = (r.micros > 0.0) ? (mb / (r.micros / 1'000'000.0)) : 0.0;
	std::cout << std::left << std::setw(14) << r.name
		<< std::left << std::setw(18) << r.scenario
		<< std::right << std::setw(12) << std::fixed << std::setprecision(2) << r.micros
		<< std::right << std::setw(14) << r.bytes
		<< std::right << std::setw(14) << std::fixed << std::setprecision(2) << mbps
		<< "\n";
}


template<typename Buf>
Result
bench_sequential_append(std::size_t N, int rounds)
{
	Result r;
	r.name            = typeid(Buf).name();
	r.scenario        = "seq_append";
	const char c      = 'x';
	auto start        = clock_t::now();
	std::size_t bytes = 0;
	for (int t = 0; t < rounds; ++t) {
		Buf b;
		b.Reserve(N);
		for (std::size_t i = 0; i < N; ++i) {
			b.AppendChar(c);
		}
		bytes += N;
	}
	auto end = clock_t::now();
	r.micros = std::chrono::duration_cast<us>(end - start).count();
	r.bytes  = bytes;
	return r;
}


template<typename Buf>
Result
bench_sequential_prepend(std::size_t N, int rounds)
{
	Result r;
	r.name            = typeid(Buf).name();
	r.scenario        = "seq_prepend";
	const char c      = 'x';
	auto start        = clock_t::now();
	std::size_t bytes = 0;
	for (int t = 0; t < rounds; ++t) {
		Buf b;
		b.Reserve(N);
		for (std::size_t i = 0; i < N; ++i) {
			b.PrependChar(c);
		}
		bytes += N;
	}
	auto end = clock_t::now();
	r.micros = std::chrono::duration_cast<us>(end - start).count();
	r.bytes  = bytes;
	return r;
}


template<typename Buf>
Result
bench_chunk_append(std::size_t N, std::size_t chunk, int rounds)
{
	Result r;
	r.name     = typeid(Buf).name();
	r.scenario = "chunk_append";
	std::string payload(chunk, 'y');
	auto start        = clock_t::now();
	std::size_t bytes = 0;
	for (int t = 0; t < rounds; ++t) {
		Buf b;
		b.Reserve(N);
		std::size_t written = 0;
		while (written < N) {
			std::size_t now = std::min(chunk, N - written);
			b.Append(payload.data(), now);
			written += now;
		}
		bytes += N;
	}
	auto end = clock_t::now();
	r.micros = std::chrono::duration_cast<us>(end - start).count();
	r.bytes  = bytes;
	return r;
}


template<typename Buf>
Result
bench_mixed(std::size_t N, std::size_t chunk, int rounds)
{
	Result r;
	r.name     = typeid(Buf).name();
	r.scenario = "mixed";
	std::string payload(chunk, 'z');
	auto start        = clock_t::now();
	std::size_t bytes = 0;
	for (int t = 0; t < rounds; ++t) {
		Buf b;
		b.Reserve(N);
		std::size_t written = 0;
		while (written < N) {
			// alternate append/prepend with small chunks
			std::size_t now = std::min(chunk, N - written);
			if ((written / chunk) % 2 == 0) {
				b.Append(payload.data(), now);
			} else {
				b.Prepend(payload.data(), now);
			}
			written += now;
		}
		bytes += N;
	}
	auto end = clock_t::now();
	r.micros = std::chrono::duration_cast<us>(end - start).count();
	r.bytes  = bytes;
	return r;
}


int
main(int argc, char **argv)
{
	// Parameters
	std::size_t N     = 100'000; // bytes per round
	int rounds        = 5; // iterations
	std::size_t chunk = 1024; // chunk size for chunked scenarios
	if (argc >= 2)
		N = static_cast<std::size_t>(std::stoull(argv[1]));
	if (argc >= 3)
		rounds = std::stoi(argv[2]);
	if (argc >= 4)
		chunk = static_cast<std::size_t>(std::stoull(argv[3]));

	std::cout << "KTE Buffer Microbenchmarks" << "\n";
	std::cout << "N=" << N << ", rounds=" << rounds << ", chunk=" << chunk << "\n\n";

	print_header();

	// Run for GapBuffer
	print_row(bench_sequential_append<GapBuffer>(N, rounds));
	print_row(bench_sequential_prepend<GapBuffer>(N, rounds));
	print_row(bench_chunk_append<GapBuffer>(N, chunk, rounds));
	print_row(bench_mixed<GapBuffer>(N, chunk, rounds));

	// Run for PieceTable
	print_row(bench_sequential_append<PieceTable>(N, rounds));
	print_row(bench_sequential_prepend<PieceTable>(N, rounds));
	print_row(bench_chunk_append<PieceTable>(N, chunk, rounds));
	print_row(bench_mixed<PieceTable>(N, chunk, rounds));

	return 0;
}