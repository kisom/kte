#include <algorithm>
#include <utility>
#include <limits>

#include "PieceTable.h"


PieceTable::PieceTable() = default;


PieceTable::PieceTable(const std::size_t initialCapacity)
{
	add_.reserve(initialCapacity);
	materialized_.reserve(initialCapacity);
}


PieceTable::PieceTable(const std::size_t initialCapacity,
                       const std::size_t piece_limit,
                       const std::size_t small_piece_threshold,
                       const std::size_t max_consolidation_bytes)
{
	add_.reserve(initialCapacity);
	materialized_.reserve(initialCapacity);
	piece_limit_             = piece_limit;
	small_piece_threshold_   = small_piece_threshold;
	max_consolidation_bytes_ = max_consolidation_bytes;
}


PieceTable::PieceTable(const PieceTable &other)
	: original_(other.original_),
	  add_(other.add_),
	  pieces_(other.pieces_),
	  materialized_(other.materialized_),
	  dirty_(other.dirty_),
	  total_size_(other.total_size_)
{
	version_ = other.version_;
	// caches are per-instance, mark invalid
	range_cache_ = {};
	find_cache_  = {};
}


PieceTable &
PieceTable::operator=(const PieceTable &other)
{
	if (this == &other)
		return *this;
	original_     = other.original_;
	add_          = other.add_;
	pieces_       = other.pieces_;
	materialized_ = other.materialized_;
	dirty_        = other.dirty_;
	total_size_   = other.total_size_;
	version_      = other.version_;
	range_cache_  = {};
	find_cache_   = {};
	return *this;
}


PieceTable::PieceTable(PieceTable &&other) noexcept
	: original_(std::move(other.original_)),
	  add_(std::move(other.add_)),
	  pieces_(std::move(other.pieces_)),
	  materialized_(std::move(other.materialized_)),
	  dirty_(other.dirty_),
	  total_size_(other.total_size_)
{
	other.dirty_      = true;
	other.total_size_ = 0;
	version_          = other.version_;
	range_cache_      = {};
	find_cache_       = {};
}


PieceTable &
PieceTable::operator=(PieceTable &&other) noexcept
{
	if (this == &other)
		return *this;
	original_         = std::move(other.original_);
	add_              = std::move(other.add_);
	pieces_           = std::move(other.pieces_);
	materialized_     = std::move(other.materialized_);
	dirty_            = other.dirty_;
	total_size_       = other.total_size_;
	other.dirty_      = true;
	other.total_size_ = 0;
	version_          = other.version_;
	range_cache_      = {};
	find_cache_       = {};
	return *this;
}


PieceTable::~PieceTable() = default;


void
PieceTable::Reserve(const std::size_t newCapacity)
{
	add_.reserve(newCapacity);
	materialized_.reserve(newCapacity);
}


// Setter to allow tuning consolidation heuristics
void
PieceTable::SetConsolidationParams(const std::size_t piece_limit,
                                   const std::size_t small_piece_threshold,
                                   const std::size_t max_consolidation_bytes)
{
	piece_limit_             = piece_limit;
	small_piece_threshold_   = small_piece_threshold;
	max_consolidation_bytes_ = max_consolidation_bytes;
}


// (removed helper) — we'll invalidate caches inline inside mutating methods


void
PieceTable::AppendChar(char c)
{
	const std::size_t start = add_.size();

	add_.push_back(c);
	addPieceBack(Source::Add, start, 1);
}


void
PieceTable::Append(const char *s, const std::size_t len)
{
	if (len == 0) {
		return;
	}

	const std::size_t start = add_.size();
	add_.append(s, len);
	addPieceBack(Source::Add, start, len);
}


void
PieceTable::Append(const PieceTable &other)
{
	// Simpler and safe: materialize "other" and append bytes
	const char *d = other.Data();
	Append(d, other.Size());
}


void
PieceTable::PrependChar(const char c)
{
	const std::size_t start = add_.size();

	add_.push_back(c);
	addPieceFront(Source::Add, start, 1);
}


void
PieceTable::Prepend(const char *s, const std::size_t len)
{
	if (len == 0) {
		return;
	}

	const std::size_t start = add_.size();

	add_.append(s, len);
	addPieceFront(Source::Add, start, len);
}


void
PieceTable::Prepend(const PieceTable &other)
{
	const char *d = other.Data();
	Prepend(d, other.Size());
}


void
PieceTable::Clear()
{
	pieces_.clear();
	add_.clear();
	materialized_.clear();
	total_size_ = 0;
	dirty_      = true;
	line_index_.clear();
	line_index_dirty_ = true;
	version_++;
	range_cache_ = {};
	find_cache_  = {};
}


void
PieceTable::addPieceBack(const Source src, const std::size_t start, const std::size_t len)
{
	if (len == 0) {
		return;
	}

	// Attempt to coalesce with last piece if contiguous and same source
	if (!pieces_.empty()) {
		Piece &last = pieces_.back();
		if (last.src == src) {
			std::size_t expectStart = last.start + last.len;

			if (expectStart == start) {
				last.len += len;
				total_size_ += len;
				dirty_ = true;
				version_++;
				range_cache_ = {};
				find_cache_  = {};
				return;
			}
		}
	}

	pieces_.push_back(Piece{src, start, len});
	total_size_ += len;
	dirty_ = true;
	InvalidateLineIndex();
	version_++;
	range_cache_ = {};
	find_cache_  = {};
}


void
PieceTable::addPieceFront(Source src, std::size_t start, std::size_t len)
{
	if (len == 0) {
		return;
	}

	// Attempt to coalesce with first piece if contiguous and same source
	if (!pieces_.empty()) {
		Piece &first = pieces_.front();
		if (first.src == src && start + len == first.start) {
			first.start = start;
			first.len += len;
			total_size_ += len;
			dirty_ = true;
			version_++;
			range_cache_ = {};
			find_cache_  = {};
			return;
		}
	}
	pieces_.insert(pieces_.begin(), Piece{src, start, len});
	total_size_ += len;
	dirty_ = true;
	InvalidateLineIndex();
	version_++;
	range_cache_ = {};
	find_cache_  = {};
}


void
PieceTable::materialize() const
{
	if (!dirty_) {
		return;
	}
	materialized_.clear();
	materialized_.reserve(total_size_ + 1);
	for (const auto &p: pieces_) {
		const std::string &src = p.src == Source::Original ? original_ : add_;
		if (p.len == 0) {
			continue;
		}

		materialized_.append(src.data() + static_cast<std::ptrdiff_t>(p.start), p.len);
	}
	// Ensure there is a null terminator present via std::string invariants
	dirty_ = false;
}


// ===== New Phase 1 implementation =====

std::pair<std::size_t, std::size_t>
PieceTable::locate(const std::size_t byte_offset) const
{
	if (byte_offset >= total_size_) {
		return {pieces_.size(), 0};
	}
	std::size_t off = byte_offset;
	for (std::size_t i = 0; i < pieces_.size(); ++i) {
		const auto &p = pieces_[i];
		if (off < p.len) {
			return {i, off};
		}
		off -= p.len;
	}
	// Should not reach here unless inconsistency; return end
	return {pieces_.size(), 0};
}


void
PieceTable::coalesceNeighbors(std::size_t index)
{
	if (pieces_.empty())
		return;
	if (index >= pieces_.size())
		index = pieces_.size() - 1;
	// Merge repeatedly with previous while contiguous and same source
	while (index > 0) {
		auto &prev = pieces_[index - 1];
		auto &curr = pieces_[index];
		if (prev.src == curr.src && prev.start + prev.len == curr.start) {
			prev.len += curr.len;
			pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(index));
			index -= 1;
		} else {
			break;
		}
	}
	// Merge repeatedly with next while contiguous and same source
	while (index + 1 < pieces_.size()) {
		auto &curr = pieces_[index];
		auto &next = pieces_[index + 1];
		if (curr.src == next.src && curr.start + curr.len == next.start) {
			curr.len += next.len;
			pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(index + 1));
		} else {
			break;
		}
	}
}


void
PieceTable::InvalidateLineIndex() const
{
	line_index_dirty_ = true;
}


void
PieceTable::RebuildLineIndex() const
{
	if (!line_index_dirty_)
		return;
	line_index_.clear();
	line_index_.push_back(0);
	std::size_t pos = 0;
	for (const auto &pc: pieces_) {
		const std::string &src = pc.src == Source::Original ? original_ : add_;
		const char *base       = src.data() + static_cast<std::ptrdiff_t>(pc.start);
		for (std::size_t j = 0; j < pc.len; ++j) {
			if (base[j] == '\n') {
				// next line starts after the newline
				line_index_.push_back(pos + j + 1);
			}
		}
		pos += pc.len;
	}
	line_index_dirty_ = false;
}


void
PieceTable::Insert(std::size_t byte_offset, const char *text, std::size_t len)
{
	if (len == 0) {
		return;
	}
	if (byte_offset > total_size_) {
		byte_offset = total_size_;
	}

	const std::size_t add_start = add_.size();
	add_.append(text, len);

	if (pieces_.empty()) {
		pieces_.push_back(Piece{Source::Add, add_start, len});
		total_size_ += len;
		dirty_ = true;
		InvalidateLineIndex();
		maybeConsolidate();
		version_++;
		range_cache_ = {};
		find_cache_  = {};
		return;
	}

	auto [idx, inner] = locate(byte_offset);
	if (idx == pieces_.size()) {
		// insert at end
		pieces_.push_back(Piece{Source::Add, add_start, len});
		total_size_ += len;
		dirty_ = true;
		InvalidateLineIndex();
		coalesceNeighbors(pieces_.size() - 1);
		maybeConsolidate();
		version_++;
		range_cache_ = {};
		find_cache_  = {};
		return;
	}

	Piece target = pieces_[idx];
	// Build replacement sequence: left, inserted, right
	std::vector<Piece> repl;
	repl.reserve(3);
	if (inner > 0) {
		repl.push_back(Piece{target.src, target.start, inner});
	}
	repl.push_back(Piece{Source::Add, add_start, len});
	const std::size_t right_len = target.len - inner;
	if (right_len > 0) {
		repl.push_back(Piece{target.src, target.start + inner, right_len});
	}

	// Replace target with repl
	pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(idx));
	pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(idx), repl.begin(), repl.end());

	total_size_ += len;
	dirty_ = true;
	InvalidateLineIndex();
	// Try coalescing around the inserted position (the inserted piece is at idx + (inner>0 ? 1 : 0))
	std::size_t ins_index = idx + (inner > 0 ? 1 : 0);
	coalesceNeighbors(ins_index);
	maybeConsolidate();
	version_++;
	range_cache_ = {};
	find_cache_  = {};
}


void
PieceTable::Delete(std::size_t byte_offset, std::size_t len)
{
	if (len == 0) {
		return;
	}
	if (byte_offset >= total_size_) {
		return;
	}
	if (byte_offset + len > total_size_) {
		len = total_size_ - byte_offset;
	}

	auto [idx, inner]     = locate(byte_offset);
	std::size_t remaining = len;

	while (remaining > 0 && idx < pieces_.size()) {
		Piece &pc             = pieces_[idx];
		std::size_t available = pc.len - inner; // bytes we can remove from this piece starting at inner
		std::size_t take      = std::min(available, remaining);

		// Compute lengths for left and right remnants
		std::size_t left_len  = inner;
		std::size_t right_len = pc.len - inner - take;
		Source src            = pc.src;
		std::size_t start     = pc.start;

		// Replace current piece with up to two remnants
		if (left_len > 0 && right_len > 0) {
			pc.len = left_len; // keep left in place
			Piece right{src, start + inner + take, right_len};
			pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(idx + 1), right);
			idx += 1; // move to right for next iteration decision
		} else if (left_len > 0) {
			pc.len = left_len;
			// no insertion; idx now points to left; move to next piece
		} else if (right_len > 0) {
			pc.start = start + inner + take;
			pc.len   = right_len;
		} else {
			// entire piece removed
			pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(idx));
			// stay at same idx for next piece
			inner = 0;
			remaining -= take;
			continue;
		}

		// After modifying current idx, next deletion continues at beginning of the next logical region
		inner = 0;
		remaining -= take;
		if (remaining == 0)
			break;
		// Move to next piece
		idx += 1;
	}

	total_size_ -= len;
	dirty_ = true;
	InvalidateLineIndex();
	if (idx < pieces_.size())
		coalesceNeighbors(idx);
	if (idx > 0)
		coalesceNeighbors(idx - 1);
	maybeConsolidate();
	version_++;
	range_cache_ = {};
	find_cache_  = {};
}


// ===== Consolidation implementation =====

void
PieceTable::appendPieceDataTo(std::string &out, const Piece &p) const
{
	if (p.len == 0)
		return;
	const std::string &src = p.src == Source::Original ? original_ : add_;
	out.append(src.data() + static_cast<std::ptrdiff_t>(p.start), p.len);
}


void
PieceTable::consolidateRange(std::size_t start_idx, std::size_t end_idx)
{
	if (start_idx >= end_idx || start_idx >= pieces_.size())
		return;
	end_idx           = std::min(end_idx, pieces_.size());
	std::size_t total = 0;
	for (std::size_t i = start_idx; i < end_idx; ++i)
		total += pieces_[i].len;
	if (total == 0)
		return;

	const std::size_t add_start = add_.size();
	std::string tmp;
	tmp.reserve(std::min<std::size_t>(total, max_consolidation_bytes_));
	for (std::size_t i = start_idx; i < end_idx; ++i)
		appendPieceDataTo(tmp, pieces_[i]);
	add_.append(tmp);

	// Replace [start_idx, end_idx) with single Add piece
	Piece consolidated{Source::Add, add_start, tmp.size()};
	pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(start_idx),
	              pieces_.begin() + static_cast<std::ptrdiff_t>(end_idx));
	pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(start_idx), consolidated);

	// total_size_ unchanged
	dirty_ = true;
	InvalidateLineIndex();
	coalesceNeighbors(start_idx);
	// Layout changed; invalidate caches/version
	version_++;
	range_cache_ = {};
	find_cache_  = {};
}


void
PieceTable::maybeConsolidate()
{
	if (pieces_.size() <= piece_limit_)
		return;

	// Find the first run of small pieces to consolidate
	std::size_t n          = pieces_.size();
	std::size_t best_start = n, best_end = n;
	std::size_t i          = 0;
	while (i < n) {
		// Skip large pieces quickly
		if (pieces_[i].len > small_piece_threshold_) {
			i++;
			continue;
		}
		std::size_t j     = i;
		std::size_t bytes = 0;
		while (j < n) {
			const auto &p = pieces_[j];
			if (p.len > small_piece_threshold_)
				break;
			if (bytes + p.len > max_consolidation_bytes_)
				break;
			bytes += p.len;
			j++;
		}
		if (j - i >= 2 && bytes > 0) {
			// consolidate runs of at least 2 pieces
			best_start = i;
			best_end   = j;
			break; // do one run per call; subsequent ops can repeat if still over limit
		}
		i = j + 1;
	}

	if (best_start < best_end) {
		consolidateRange(best_start, best_end);
	}
}


std::size_t
PieceTable::LineCount() const
{
	RebuildLineIndex();
	return line_index_.empty() ? 0 : line_index_.size();
}


std::pair<std::size_t, std::size_t>
PieceTable::GetLineRange(std::size_t line_num) const
{
	RebuildLineIndex();
	if (line_index_.empty())
		return {0, 0};
	if (line_num >= line_index_.size())
		return {0, 0};
	std::size_t start = line_index_[line_num];
	std::size_t end   = (line_num + 1 < line_index_.size()) ? line_index_[line_num + 1] : total_size_;
	return {start, end};
}


std::string
PieceTable::GetLine(std::size_t line_num) const
{
	auto [start, end] = GetLineRange(line_num);
	if (end < start)
		return std::string();
	// Trim trailing '\n'
	if (end > start) {
		// To check last char, we can get it via GetRange of len 1 at end-1 without materializing whole
		std::string last = GetRange(end - 1, 1);
		if (!last.empty() && last[0] == '\n') {
			end -= 1;
		}
	}
	return GetRange(start, end - start);
}


std::pair<std::size_t, std::size_t>
PieceTable::ByteOffsetToLineCol(std::size_t byte_offset) const
{
	if (byte_offset > total_size_)
		byte_offset = total_size_;
	RebuildLineIndex();
	if (line_index_.empty())
		return {0, 0};
	auto it         = std::upper_bound(line_index_.begin(), line_index_.end(), byte_offset);
	std::size_t row = (it == line_index_.begin()) ? 0 : static_cast<std::size_t>((it - line_index_.begin()) - 1);
	std::size_t col = byte_offset - line_index_[row];
	return {row, col};
}


std::size_t
PieceTable::LineColToByteOffset(std::size_t row, std::size_t col) const
{
	RebuildLineIndex();
	if (line_index_.empty())
		return 0;
	if (row >= line_index_.size())
		return total_size_;
	std::size_t start = line_index_[row];
	std::size_t end   = (row + 1 < line_index_.size()) ? line_index_[row + 1] : total_size_;
	// Clamp col to line length excluding trailing newline
	if (end > start) {
		std::string last = GetRange(end - 1, 1);
		if (!last.empty() && last[0] == '\n') {
			end -= 1;
		}
	}
	std::size_t target = start + std::min(col, end - start);
	return target;
}


std::string
PieceTable::GetRange(std::size_t byte_offset, std::size_t len) const
{
	if (byte_offset >= total_size_ || len == 0)
		return std::string();
	if (byte_offset + len > total_size_)
		len = total_size_ - byte_offset;

	// Fast path: return cached value if version/offset/len match
	if (range_cache_.valid && range_cache_.version == version_ &&
	    range_cache_.off == byte_offset && range_cache_.len == len) {
		return range_cache_.data;
	}

	std::string out;
	out.reserve(len);
	if (!dirty_) {
		// Already materialized; slice directly
		out.assign(materialized_.data() + static_cast<std::ptrdiff_t>(byte_offset), len);
	} else {
		// Assemble substring directly from pieces without full materialization
		auto [idx, inner]     = locate(byte_offset);
		std::size_t remaining = len;
		while (remaining > 0 && idx < pieces_.size()) {
			const auto &p          = pieces_[idx];
			const std::string &src = (p.src == Source::Original) ? original_ : add_;
			std::size_t take       = std::min<std::size_t>(p.len - inner, remaining);
			if (take > 0) {
				const char *base = src.data() + static_cast<std::ptrdiff_t>(p.start + inner);
				out.append(base, take);
				remaining -= take;
				inner = 0;
				idx += 1;
			} else {
				break;
			}
		}
	}

	// Update cache
	range_cache_.valid   = true;
	range_cache_.version = version_;
	range_cache_.off     = byte_offset;
	range_cache_.len     = len;
	range_cache_.data    = out;
	return out;
}


std::size_t
PieceTable::Find(const std::string &needle, std::size_t start) const
{
	if (needle.empty())
		return start <= total_size_ ? start : std::numeric_limits<std::size_t>::max();
	if (start > total_size_)
		return std::numeric_limits<std::size_t>::max();
	if (find_cache_.valid &&
	    find_cache_.version == version_ &&
	    find_cache_.needle == needle &&
	    find_cache_.start == start) {
		return find_cache_.result;
	}

	materialize();
	auto pos = materialized_.find(needle, start);
	if (pos == std::string::npos)
		pos = std::numeric_limits<std::size_t>::max();
	// Update cache
	find_cache_.valid   = true;
	find_cache_.version = version_;
	find_cache_.needle  = needle;
	find_cache_.start   = start;
	find_cache_.result  = pos;
	return pos;
}
