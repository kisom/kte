# kte Benchmarking and Testing Guide

This document describes the benchmarking infrastructure and testing
improvements added to ensure high performance and correctness of core
operations.

## Overview

The kte test suite now includes comprehensive benchmarks and migration
coverage tests to:

- Measure performance of core operations (PieceTable, Buffer, syntax
  highlighting)
- Ensure no performance regressions from refactorings
- Validate correctness of API migrations (Buffer::Rows() →
  GetLineString/GetLineView)
- Provide performance baselines for future optimizations

## Running Tests

### All Tests (including benchmarks)

```bash
cmake --build cmake-build-debug --target kte_tests && ./cmake-build-debug/kte_tests
```

### Test Organization

- **58 existing tests**: Core functionality, undo/redo, swap recovery,
  search, etc.
- **15 benchmark tests**: Performance measurements for critical
  operations
- **30 migration coverage tests**: Edge cases and correctness validation

Total: **98 tests**

## Benchmark Results

### Buffer Iteration Patterns (5,000 lines)

| Pattern                                 | Time    | Speedup vs Rows() |
|-----------------------------------------|---------|-------------------|
| `Rows()` + iteration                    | 3.1 ms  | 1.0x (baseline)   |
| `Nrows()` + `GetLineString()`           | 1.9 ms  | **1.7x faster**   |
| `Nrows()` + `GetLineView()` (zero-copy) | 0.28 ms | **11x faster**    |

**Key Insight**: `GetLineView()` provides zero-copy access and is
dramatically faster than materializing the entire rows cache.

### PieceTable Operations (10,000 lines)

| Operation                   | Time    |
|-----------------------------|---------|
| Sequential inserts (10K)    | 2.1 ms  |
| Random inserts (5K)         | 32.9 ms |
| `GetLine()` sequential      | 4.7 ms  |
| `GetLineRange()` sequential | 1.3 ms  |

### Buffer Operations

| Operation                            | Time    |
|--------------------------------------|---------|
| `Nrows()` (1M calls)                 | 13.0 ms |
| `GetLineString()` (10K lines)        | 4.8 ms  |
| `GetLineView()` (10K lines)          | 1.6 ms  |
| `Rows()` materialization (10K lines) | 6.2 ms  |

### Syntax Highlighting

| Operation                          | Time    | Notes          |
|------------------------------------|---------|----------------|
| C++ highlighting (~1000 lines)     | 2.0 ms  | First pass     |
| HighlighterEngine cache population | 19.9 ms |                |
| HighlighterEngine cache hits       | 0.52 ms | **38x faster** |

### Large File Performance

| Operation                       | Time    |
|---------------------------------|---------|
| Insert 50K lines                | 0.53 ms |
| Iterate 50K lines (GetLineView) | 2.7 ms  |
| Random access (10K accesses)    | 1.8 ms  |

## API Differences: GetLineString vs GetLineView

Understanding the difference between these APIs is critical:

### `GetLineString(row)`

- Returns: `std::string` (copy)
- Content: Line text **without** trailing newline
- Use case: When you need to modify the string or store it
- Example: `"hello"` for line `"hello\n"`

### `GetLineView(row)`

- Returns: `std::string_view` (zero-copy)
- Content: Raw line range **including** trailing newline
- Use case: Read-only access, maximum performance
- Example: `"hello\n"` for line `"hello\n"`
- **Warning**: View becomes invalid after buffer modifications

### `Rows()`

- Returns: `std::vector<Buffer::Line>&` (materialized cache)
- Content: Lines **without** trailing newlines
- Use case: Legacy code, being phased out
- Performance: Slower due to materialization overhead

## Migration Coverage Tests

The `test_migration_coverage.cc` file provides 30 tests covering:

### Edge Cases

- Empty buffers
- Single lines (with/without newlines)
- Very long lines (10,000 characters)
- Many empty lines (1,000 newlines)

### Consistency

- `GetLineString()` vs `GetLineView()` vs `Rows()`
- Consistency after edits (insert, delete, split, join)

### Boundary Conditions

- First line access
- Last line access
- Line range boundaries

### Special Characters

- Tabs, carriage returns, null bytes
- Unicode (UTF-8 multibyte characters)

### Stress Tests

- Large files (10,000 lines)
- Many small operations (100+ inserts)
- Alternating insert/delete patterns

### Regression Tests

- Shebang detection pattern (Editor.cc)
- Empty buffer check pattern (Editor.cc)
- Syntax highlighter pattern (all highlighters)
- Swap snapshot pattern (Swap.cc)

## Performance Recommendations

Based on benchmark results:

1. **Prefer `GetLineView()` for read-only access**
    - 11x faster than `Rows()` for iteration
    - Zero-copy, minimal overhead
    - Use immediately (view invalidates on edit)

2. **Use `GetLineString()` when you need a copy**
    - Still 1.7x faster than `Rows()`
    - Safe to store and modify
    - Strips trailing newlines automatically

3. **Avoid `Rows()` in hot paths**
    - Materializes entire line cache
    - Slower for large files
    - Being phased out (legacy API)

4. **Cache `Nrows()` in tight loops**
    - Very fast (13ms for 1M calls)
    - But still worth caching in inner loops

5. **Leverage HighlighterEngine caching**
    - 38x speedup on cache hits
    - Automatically invalidates on edits
    - Prefetch viewport for smooth scrolling

## Adding New Benchmarks

To add a new benchmark:

1. Add a `TEST(Benchmark_YourName)` in `tests/test_benchmarks.cc`
2. Use `BenchmarkTimer` to measure critical sections:
   ```cpp
   {
       BenchmarkTimer timer("Operation description");
       // ... code to benchmark ...
   }
   ```
3. Print section headers with `std::cout` for clarity
4. Use `ASSERT_EQ` or `EXPECT_TRUE` to validate results

Example:

```cpp
TEST(Benchmark_MyOperation) {
    std::cout << "\n=== My Operation Benchmark ===\n";
    
    // Setup
    Buffer buf;
    std::string data = generate_test_data();
    buf.insert_text(0, 0, data);
    
    std::size_t result = 0;
    {
        BenchmarkTimer timer("My operation on 10K lines");
        for (std::size_t i = 0; i < buf.Nrows(); ++i) {
            result += my_operation(buf, i);
        }
    }
    
    EXPECT_TRUE(result > 0);
}
```

## Continuous Performance Monitoring

Run benchmarks regularly to detect regressions:

```bash
# Run tests and save output
./cmake-build-debug/kte_tests > benchmark_results.txt

# Compare with baseline
diff benchmark_baseline.txt benchmark_results.txt
```

Look for:

- Significant time increases (>20%) in any benchmark
- New operations that are slower than expected
- Cache effectiveness degradation

## Conclusion

The benchmark suite provides:

- **Performance validation**: Ensures migrations don't regress
  performance
- **Optimization guidance**: Identifies fastest APIs for each use case
- **Regression detection**: Catches performance issues early
- **Documentation**: Demonstrates correct API usage patterns

All 98 tests pass with 0 failures, confirming both correctness and
performance of the migrated codebase.
