/*
 * PieceTable.h - Alternative to GapBuffer using a piece table representation
 *
 * PieceTable is kte's core text storage data structure. It provides efficient
 * insert/delete operations without copying the entire buffer by maintaining a
 * sequence of "pieces" that reference ranges in two underlying buffers:
 * - original_: A loaded file's content (AdoptOriginal), never modified
 * - add_: All text added during editing
 *
 * Key advantages:
 * - O(1) append/prepend operations (common case)
 * - O(n) insert/delete at arbitrary positions (n = number of pieces, not bytes)
 * - Efficient undo: just restore the piece list
 * - Memory efficient: no gap buffer waste
 *
 * Performance characteristics:
 * - Piece count grows with edit operations; automatic consolidation prevents unbounded growth
 * - Materialization (Data() call) is O(total_size) but cached until next edit;
 *   a single-piece table (an unedited file) is viewed in place, not copied
 * - Line index is lazily rebuilt on first line-based query after edits
 * - Range and Find operations use lightweight caches for repeated queries
 *
 * API evolution:
 * 1. Legacy API (GapBuffer compatibility):
 *    - Append/Prepend: Build content sequentially
 *    - Data(): Materialize entire buffer
 *
 * 2. New buffer-wide API (Phase 1):
 *    - Insert/Delete: Edit at arbitrary byte offsets
 *    - Line-based queries: LineCount, GetLine, GetLineRange
 *    - Position conversion: ByteOffsetToLineCol, LineColToByteOffset
 *    - Efficient extraction: GetRange, Find, WriteToStream
 *
 * Implementation notes:
 * - Consolidation heuristics prevent piece fragmentation (configurable via SetConsolidationParams)
 * - Not thread-safe: even reads update caches. Buffers are used from the
 *   main thread only (the swap writer thread gets copies of the bytes).
 * - Version tracking invalidates caches on mutations
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <ostream>
#include <vector>
#include <limits>
#include <string_view>

#include "TextSpan.h"


class PieceTable {
public:
	PieceTable();

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

	void AppendChar(char c);

	void Append(const char *s, std::size_t len);

	void Append(const PieceTable &other);

	void PrependChar(char c);

	void Prepend(const char *s, std::size_t len);

	void Prepend(const PieceTable &other);

	// Content management
	void Clear();

	// Replace the content with `bytes`, taken over without copying (a loaded
	// file). Undo spans into the old storage are invalidated, as by Clear().
	void AdoptOriginal(std::string &&bytes);

	// Accessors
	// The whole text, contiguous; nullptr when empty. Valid until the next
	// modification.
	[[nodiscard]] const char *Data() const
	{
		const std::string_view v = ContentView();
		return v.empty() ? nullptr : v.data();
	}


	// The whole text as one view: in place for a single piece, else the
	// materialized copy (built once per modification). Valid until the next
	// modification.
	[[nodiscard]] std::string_view ContentView() const;

	// A view of [byte_offset, byte_offset + len), clamped to the content: in
	// place when the range lies within one piece (no copy), else into the
	// materialized text. Valid until the next modification.
	[[nodiscard]] std::string_view View(std::size_t byte_offset, std::size_t len) const;

	// Call fn(const char *data, std::size_t len) for each piece, in order:
	// the content without materializing it.
	template<typename Fn>
	void ForEachChunk(Fn &&fn) const
	{
		for (const Piece &p: pieces_) {
			if (p.len > 0)
				fn((p.src == Source::Original ? original_ : add_).data() + p.start, p.len);
		}
	}


	[[nodiscard]] std::size_t Size() const
	{
		return total_size_;
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

	// Stream out content without materializing the entire buffer
	void WriteToStream(std::ostream &out) const;

	// ===== Span access (see TextSpan.h) =====
	// The storage spans backing [byte_offset, byte_offset + len).
	[[nodiscard]] std::vector<TextSpan> SpansInRange(std::size_t byte_offset, std::size_t len) const;

	// Call fn(const char *data, std::size_t len) for each span's bytes, in order.
	template<typename Fn>
	void VisitSpans(const std::vector<TextSpan> &spans, Fn &&fn) const
	{
		for (const TextSpan &sp: spans) {
			const std::string &src = sp.add ? add_ : original_;
			if (sp.len > 0 && sp.start + sp.len <= src.size())
				fn(src.data() + sp.start, sp.len);
		}
	}


	// Whether the spans hold exactly `text`.
	[[nodiscard]] bool SpansEqual(const std::vector<TextSpan> &spans, std::string_view text) const;

	// Insert the spans' bytes at byte_offset without copying them into
	// storage again (the new pieces reference the existing bytes).
	void InsertSpans(std::size_t byte_offset, const std::vector<TextSpan> &spans);

	// Deletions of at least kCaptureDeletedMin bytes remember the spans they
	// removed; this returns them if the most recent deletion was exactly
	// [byte_offset, byte_offset + len), else nothing. Consumes the capture.
	static constexpr std::size_t kCaptureDeletedMin = 4096;

	[[nodiscard]] std::vector<TextSpan> TakeDeletedSpans(std::size_t byte_offset, std::size_t len);

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

	// Keep a valid line index in step with an edit instead of rescanning the
	// whole buffer on the next query. No-ops if the index is already dirty.
	void lineIndexOnInsert(std::size_t offset, const char *text, std::size_t len) const;

	void lineIndexOnDelete(std::size_t offset, std::size_t len) const;

	void RebuildLineIndex() const;

	// Underlying storages
	std::string original_; // a loaded file's bytes (AdoptOriginal)
	std::string add_;
	std::vector<Piece> pieces_;

	mutable std::string materialized_;
	mutable bool dirty_ = true;
	// Monotonic content version. Increment on any mutation that affects content layout
	mutable std::uint64_t version_ = 0;
	std::size_t total_size_        = 0;

	// End offset of each piece (prefix sums) for binary-search lookup,
	// valid while piece_ends_version_ == version_ (every mutation bumps it).
	mutable std::vector<std::size_t> piece_ends_;
	mutable std::uint64_t piece_ends_version_ = std::numeric_limits<std::uint64_t>::max();

	// Cached line index: starting byte offset of each line (always contains at least 1 entry: 0)
	mutable std::vector<std::size_t> line_index_;
	mutable bool line_index_dirty_ = true;
	// A shift not yet applied to line_index_: entries from index
	// line_shift_from_ on are really line_index_[i] + line_shift_by_ (mod
	// 2^64, so it may be negative). An edit moves every later line start;
	// applying that at once cost O(lines) per keystroke, so consecutive edits
	// on one line only adjust the pending shift.
	mutable std::size_t line_shift_from_ = std::numeric_limits<std::size_t>::max();
	mutable std::size_t line_shift_by_   = 0;

	// Start of line i (i < line_index_.size()), with the pending shift.
	[[nodiscard]] std::size_t lineStart(std::size_t i) const
	{
		return i >= line_shift_from_ ? line_index_[i] + line_shift_by_ : line_index_[i];
	}


	// Index of the first line starting after byte_offset.
	[[nodiscard]] std::size_t lineUpperBound(std::size_t byte_offset) const;

	// Fold the pending shift into line_index_.
	void applyLineShift() const;

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

	struct DeletedCapture {
		bool valid            = false;
		std::size_t offset    = 0;
		std::size_t len       = 0;
		std::vector<TextSpan> spans;
	} last_deleted_;

	mutable RangeCache range_cache_;
	mutable FindCache find_cache_;
};