/*
 * PieceTable.h - Alternative to GapBuffer using a piece table representation
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <limits>


class PieceTable {
public:
	PieceTable();

	explicit PieceTable(std::size_t initialCapacity);

	// Advanced constructor allowing configuration of consolidation heuristics
	PieceTable(std::size_t initialCapacity,
	           std::size_t piece_limit,
	           std::size_t small_piece_threshold,
	           std::size_t max_consolidation_bytes);

	PieceTable(const PieceTable &other);

	PieceTable &operator=(const PieceTable &other);

	PieceTable(PieceTable &&other) noexcept;

	PieceTable &operator=(PieceTable &&other) noexcept;

	~PieceTable();

	// Public API mirrors GapBuffer
	void Reserve(std::size_t newCapacity);

	void AppendChar(char c);

	void Append(const char *s, std::size_t len);

	void Append(const PieceTable &other);

	void PrependChar(char c);

	void Prepend(const char *s, std::size_t len);

	void Prepend(const PieceTable &other);

	// Content management
	void Clear();

	// Accessors
	char *Data()
	{
		materialize();
		return materialized_.empty() ? nullptr : materialized_.data();
	}


	[[nodiscard]] const char *Data() const
	{
		const_cast<PieceTable *>(this)->materialize();
		return materialized_.empty() ? nullptr : materialized_.data();
	}


	[[nodiscard]] std::size_t Size() const
	{
		return total_size_;
	}


	[[nodiscard]] std::size_t Capacity() const
	{
		// Capacity for piece table isn't directly meaningful; report materialized capacity
		return materialized_.capacity();
	}


	// ===== New buffer-wide API (Phase 1) =====
	// Byte-based editing operations
	void Insert(std::size_t byte_offset, const char *text, std::size_t len);

	void Delete(std::size_t byte_offset, std::size_t len);

	// Line-based queries
	[[nodiscard]] std::size_t LineCount() const; // number of logical lines
	[[nodiscard]] std::string GetLine(std::size_t line_num) const;

	[[nodiscard]] std::pair<std::size_t, std::size_t> GetLineRange(std::size_t line_num) const; // [start,end)

	// Position conversion
	[[nodiscard]] std::pair<std::size_t, std::size_t> ByteOffsetToLineCol(std::size_t byte_offset) const;

	[[nodiscard]] std::size_t LineColToByteOffset(std::size_t row, std::size_t col) const;

	// Substring extraction
	[[nodiscard]] std::string GetRange(std::size_t byte_offset, std::size_t len) const;

	// Simple search utility; returns byte offset or npos
	[[nodiscard]] std::size_t Find(const std::string &needle, std::size_t start = 0) const;

	// Heuristic configuration
	void SetConsolidationParams(std::size_t piece_limit,
	                            std::size_t small_piece_threshold,
	                            std::size_t max_consolidation_bytes);

private:
	enum class Source : unsigned char { Original, Add };

	struct Piece {
		Source src;
		std::size_t start;
		std::size_t len;
	};

	void addPieceBack(Source src, std::size_t start, std::size_t len);

	void addPieceFront(Source src, std::size_t start, std::size_t len);

	void materialize() const;

	// Helper: locate piece index and inner offset for a global byte offset
	[[nodiscard]] std::pair<std::size_t, std::size_t> locate(std::size_t byte_offset) const;

	// Helper: try to coalesce neighboring pieces around index
	void coalesceNeighbors(std::size_t index);

	// Consolidation helpers and heuristics
	void maybeConsolidate();

	void consolidateRange(std::size_t start_idx, std::size_t end_idx);

	void appendPieceDataTo(std::string &out, const Piece &p) const;

	// Line index support (rebuilt lazily on demand)
	void InvalidateLineIndex() const;

	void RebuildLineIndex() const;

	// Underlying storages
	std::string original_; // unused for builder use-case, but kept for API symmetry
	std::string add_;
	std::vector<Piece> pieces_;

	mutable std::string materialized_;
	mutable bool dirty_ = true;
	// Monotonic content version. Increment on any mutation that affects content layout
	mutable std::uint64_t version_ = 0;
	std::size_t total_size_        = 0;

	// Cached line index: starting byte offset of each line (always contains at least 1 entry: 0)
	mutable std::vector<std::size_t> line_index_;
	mutable bool line_index_dirty_ = true;

	// Heuristic knobs
	std::size_t piece_limit_             = 4096; // trigger consolidation when exceeded
	std::size_t small_piece_threshold_   = 64; // bytes
	std::size_t max_consolidation_bytes_ = 4096; // cap per consolidation run

	// Lightweight caches to avoid redundant work when callers query the same range repeatedly
	struct RangeCache {
		bool valid            = false;
		std::uint64_t version = 0;
		std::size_t off       = 0;
		std::size_t len       = 0;
		std::string data;
	};

	struct FindCache {
		bool valid            = false;
		std::uint64_t version = 0;
		std::string needle;
		std::size_t start  = 0;
		std::size_t result = std::numeric_limits<std::size_t>::max();
	};

	mutable RangeCache range_cache_;
	mutable FindCache find_cache_;
};
