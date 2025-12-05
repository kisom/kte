/*
 * PieceTable.h - Alternative to GapBuffer using a piece table representation
 */
#pragma once
#include <cstddef>
#include <string>
#include <vector>


class PieceTable {
public:
	PieceTable();

	explicit PieceTable(std::size_t initialCapacity);

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

	// Line index support (rebuilt lazily on demand)
	void InvalidateLineIndex() const;

	void RebuildLineIndex() const;

	// Underlying storages
	std::string original_; // unused for builder use-case, but kept for API symmetry
	std::string add_;
	std::vector<Piece> pieces_;

	mutable std::string materialized_;
	mutable bool dirty_     = true;
	std::size_t total_size_ = 0;

	// Cached line index: starting byte offset of each line (always contains at least 1 entry: 0)
	mutable std::vector<std::size_t> line_index_;
	mutable bool line_index_dirty_ = true;
};