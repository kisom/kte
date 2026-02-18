/*
 * test_benchmarks.cc - Performance benchmarks for core kte operations
 *
 * This file measures the performance of critical operations to ensure
 * that migrations and refactorings don't introduce performance regressions.
 *
 * Benchmarks cover:
 * - PieceTable operations (insert, delete, GetLine, GetLineRange)
 * - Buffer operations (Nrows, GetLineString, GetLineView)
 * - Iteration patterns (comparing old Rows() vs new GetLineString/GetLineView)
 * - Syntax highlighting on large files
 *
 * Each benchmark reports execution time in milliseconds.
 */
#include "Test.h"
#include "Buffer.h"
#include "PieceTable.h"
#include "syntax/CppHighlighter.h"
#include "syntax/HighlighterEngine.h"
#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {
// Benchmark timing utility
class BenchmarkTimer {
public:
	BenchmarkTimer(const char *name) : name_(name), start_(std::chrono::high_resolution_clock::now()) {}


	~BenchmarkTimer()
	{
		auto end      = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
		double ms     = duration.count() / 1000.0;
		std::cout << "  [BENCH] " << name_ << ": " << ms << " ms\n";
	}

private:
	const char *name_;
	std::chrono::high_resolution_clock::time_point start_;
};

// Generate test data
std::string
generate_large_file(std::size_t num_lines, std::size_t avg_line_length)
{
	std::mt19937 rng(42);
	std::string result;
	result.reserve(num_lines * (avg_line_length + 1));

	for (std::size_t i = 0; i < num_lines; ++i) {
		std::size_t line_len = avg_line_length + (rng() % 20) - 10; // ±10 chars variation
		for (std::size_t j = 0; j < line_len; ++j) {
			char c = 'a' + (rng() % 26);
			result.push_back(c);
		}
		result.push_back('\n');
	}
	return result;
}


std::string
generate_cpp_code(std::size_t num_lines)
{
	std::ostringstream oss;
	oss << "#include <iostream>\n";
	oss << "#include <vector>\n";
	oss << "#include <string>\n\n";
	oss << "namespace test {\n";

	for (std::size_t i = 0; i < num_lines / 10; ++i) {
		oss << "class TestClass" << i << " {\n";
		oss << "public:\n";
		oss << "    void method" << i << "() {\n";
		oss << "        // Comment line\n";
		oss << "        int x = " << i << ";\n";
		oss << "        std::string s = \"test string\";\n";
		oss << "        for (int j = 0; j < 100; ++j) {\n";
		oss << "            x += j;\n";
		oss << "        }\n";
		oss << "    }\n";
		oss << "};\n\n";
	}
	oss << "} // namespace test\n";
	return oss.str();
}
} // anonymous namespace

// ============================================================================
// PieceTable Benchmarks
// ============================================================================

TEST (Benchmark_PieceTable_Sequential_Inserts)
{
	std::cout << "\n=== PieceTable Sequential Insert Benchmark ===\n";
	PieceTable pt;
	const std::size_t num_ops  = 10000;
	const char *text           = "line\n";
	const std::size_t text_len = 5;

	{
		BenchmarkTimer timer("10K sequential inserts at end");
		for (std::size_t i = 0; i < num_ops; ++i) {
			pt.Insert(pt.Size(), text, text_len);
		}
	}

	ASSERT_EQ(pt.LineCount(), num_ops + 1); // +1 for final empty line
}


TEST (Benchmark_PieceTable_Random_Inserts)
{
	std::cout << "\n=== PieceTable Random Insert Benchmark ===\n";
	PieceTable pt;
	const std::size_t num_ops  = 5000;
	const char *text           = "xyz\n";
	const std::size_t text_len = 4;
	std::mt19937 rng(123);

	// Pre-populate with some content
	std::string initial = generate_large_file(1000, 50);
	pt.Insert(0, initial.data(), initial.size());

	{
		BenchmarkTimer timer("5K random inserts");
		for (std::size_t i = 0; i < num_ops; ++i) {
			std::size_t pos = rng() % (pt.Size() + 1);
			pt.Insert(pos, text, text_len);
		}
	}
}


TEST (Benchmark_PieceTable_GetLine_Sequential)
{
	std::cout << "\n=== PieceTable GetLine Sequential Benchmark ===\n";
	PieceTable pt;
	std::string data = generate_large_file(10000, 80);
	pt.Insert(0, data.data(), data.size());

	std::size_t total_chars = 0;
	{
		BenchmarkTimer timer("GetLine on 10K lines (sequential)");
		for (std::size_t i = 0; i < pt.LineCount(); ++i) {
			std::string line = pt.GetLine(i);
			total_chars      += line.size();
		}
	}

	EXPECT_TRUE(total_chars > 0);
}


TEST (Benchmark_PieceTable_GetLineRange_Sequential)
{
	std::cout << "\n=== PieceTable GetLineRange Sequential Benchmark ===\n";
	PieceTable pt;
	std::string data = generate_large_file(10000, 80);
	pt.Insert(0, data.data(), data.size());

	std::size_t total_ranges = 0;
	{
		BenchmarkTimer timer("GetLineRange on 10K lines (sequential)");
		for (std::size_t i = 0; i < pt.LineCount(); ++i) {
			auto range   = pt.GetLineRange(i);
			total_ranges += (range.second - range.first);
		}
	}

	EXPECT_TRUE(total_ranges > 0);
}


// ============================================================================
// Buffer Benchmarks
// ============================================================================

TEST (Benchmark_Buffer_Nrows_Repeated_Calls)
{
	std::cout << "\n=== Buffer Nrows Benchmark ===\n";
	Buffer buf;
	std::string data = generate_large_file(10000, 80);
	buf.insert_text(0, 0, data);

	std::size_t sum = 0;
	{
		BenchmarkTimer timer("1M calls to Nrows()");
		for (int i = 0; i < 1000000; ++i) {
			sum += buf.Nrows();
		}
	}

	EXPECT_TRUE(sum > 0);
}


TEST (Benchmark_Buffer_GetLineString_Sequential)
{
	std::cout << "\n=== Buffer GetLineString Sequential Benchmark ===\n";
	Buffer buf;
	std::string data = generate_large_file(10000, 80);
	buf.insert_text(0, 0, data);

	std::size_t total_chars = 0;
	{
		BenchmarkTimer timer("GetLineString on 10K lines");
		for (std::size_t i = 0; i < buf.Nrows(); ++i) {
			std::string line = buf.GetLineString(i);
			total_chars      += line.size();
		}
	}

	EXPECT_TRUE(total_chars > 0);
}


TEST (Benchmark_Buffer_GetLineView_Sequential)
{
	std::cout << "\n=== Buffer GetLineView Sequential Benchmark ===\n";
	Buffer buf;
	std::string data = generate_large_file(10000, 80);
	buf.insert_text(0, 0, data);

	std::size_t total_chars = 0;
	{
		BenchmarkTimer timer("GetLineView on 10K lines");
		for (std::size_t i = 0; i < buf.Nrows(); ++i) {
			auto view   = buf.GetLineView(i);
			total_chars += view.size();
		}
	}

	EXPECT_TRUE(total_chars > 0);
}


TEST (Benchmark_Buffer_Rows_Materialization)
{
	std::cout << "\n=== Buffer Rows() Materialization Benchmark ===\n";
	Buffer buf;
	std::string data = generate_large_file(10000, 80);
	buf.insert_text(0, 0, data);

	std::size_t total_chars = 0;
	{
		BenchmarkTimer timer("Rows() materialization + iteration on 10K lines");
		const auto &rows = buf.Rows();
		for (std::size_t i = 0; i < rows.size(); ++i) {
			total_chars += rows[i].size();
		}
	}

	EXPECT_TRUE(total_chars > 0);
}


TEST (Benchmark_Buffer_Iteration_Comparison)
{
	std::cout << "\n=== Buffer Iteration Pattern Comparison ===\n";
	Buffer buf;
	std::string data = generate_large_file(5000, 80);
	buf.insert_text(0, 0, data);

	std::size_t sum1 = 0, sum2 = 0, sum3 = 0;

	// Pattern 1: Old style with Rows()
	{
		BenchmarkTimer timer("Pattern 1: Rows() + iteration");
		const auto &rows = buf.Rows();
		for (std::size_t i = 0; i < rows.size(); ++i) {
			sum1 += rows[i].size();
		}
	}

	// Pattern 2: New style with GetLineString
	{
		BenchmarkTimer timer("Pattern 2: Nrows() + GetLineString");
		for (std::size_t i = 0; i < buf.Nrows(); ++i) {
			sum2 += buf.GetLineString(i).size();
		}
	}

	// Pattern 3: New style with GetLineView (zero-copy)
	{
		BenchmarkTimer timer("Pattern 3: Nrows() + GetLineView (zero-copy)");
		for (std::size_t i = 0; i < buf.Nrows(); ++i) {
			sum3 += buf.GetLineView(i).size();
		}
	}

	// sum1 and sum2 should match (both strip newlines)
	ASSERT_EQ(sum1, sum2);
	// sum3 includes newlines, so it will be larger
	EXPECT_TRUE(sum3 > sum2);
}


// ============================================================================
// Syntax Highlighting Benchmarks
// ============================================================================

TEST (Benchmark_Syntax_CppHighlighter_Large_File)
{
	std::cout << "\n=== Syntax Highlighting Benchmark ===\n";
	Buffer buf;
	std::string cpp_code = generate_cpp_code(1000);
	buf.insert_text(0, 0, cpp_code);
	buf.EnsureHighlighter();

	auto highlighter        = std::make_unique<kte::CppHighlighter>();
	std::size_t total_spans = 0;

	{
		BenchmarkTimer timer("C++ highlighting on ~1000 lines");
		for (std::size_t i = 0; i < buf.Nrows(); ++i) {
			std::vector<kte::HighlightSpan> spans;
			highlighter->HighlightLine(buf, static_cast<int>(i), spans);
			total_spans += spans.size();
		}
	}

	EXPECT_TRUE(total_spans > 0);
}


TEST (Benchmark_Syntax_HighlighterEngine_Cached)
{
	std::cout << "\n=== HighlighterEngine Cache Benchmark ===\n";
	Buffer buf;
	std::string cpp_code = generate_cpp_code(1000);
	buf.insert_text(0, 0, cpp_code);
	buf.EnsureHighlighter();

	auto *engine = buf.Highlighter();
	if (engine) {
		engine->SetHighlighter(std::make_unique<kte::CppHighlighter>());

		// First pass: populate cache
		{
			BenchmarkTimer timer("First pass (cache population)");
			for (std::size_t i = 0; i < buf.Nrows(); ++i) {
				engine->GetLine(buf, static_cast<int>(i), buf.Version());
			}
		}

		// Second pass: use cache
		{
			BenchmarkTimer timer("Second pass (cache hits)");
			for (std::size_t i = 0; i < buf.Nrows(); ++i) {
				engine->GetLine(buf, static_cast<int>(i), buf.Version());
			}
		}
	}
}


// ============================================================================
// Large File Stress Tests
// ============================================================================

TEST (Benchmark_Large_File_50K_Lines)
{
	std::cout << "\n=== Large File (50K lines) Benchmark ===\n";
	Buffer buf;
	std::string data = generate_large_file(50000, 80);

	{
		BenchmarkTimer timer("Insert 50K lines");
		buf.insert_text(0, 0, data);
	}

	ASSERT_EQ(buf.Nrows(), (std::size_t) 50001); // +1 for final line

	std::size_t total = 0;
	{
		BenchmarkTimer timer("Iterate 50K lines with GetLineView");
		for (std::size_t i = 0; i < buf.Nrows(); ++i) {
			total += buf.GetLineView(i).size();
		}
	}

	EXPECT_TRUE(total > 0);
}


TEST (Benchmark_Random_Access_Pattern)
{
	std::cout << "\n=== Random Access Pattern Benchmark ===\n";
	Buffer buf;
	std::string data = generate_large_file(10000, 80);
	buf.insert_text(0, 0, data);

	std::mt19937 rng(456);
	std::size_t total = 0;

	{
		BenchmarkTimer timer("10K random line accesses with GetLineView");
		for (int i = 0; i < 10000; ++i) {
			std::size_t line = rng() % buf.Nrows();
			total            += buf.GetLineView(line).size();
		}
	}

	EXPECT_TRUE(total > 0);
}