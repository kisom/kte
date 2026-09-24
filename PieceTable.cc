#include <algorithm>
#include <cstring>
#include <utility>
#include <limits>
#include <ostream>

#include "PieceTable.h"


PieceTable::PieceTable() = default;


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
	last_deleted_ = {};
	piece_ends_version_ = std::numeric_limits<std::uint64_t>::max();
	InvalidateLineIndex();
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
	other.InvalidateLineIndex(); // its index described the moved-away text
	other.piece_ends_version_ = std::numeric_limits<std::uint64_t>::max();
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
	other.InvalidateLineIndex(); // its index described the moved-away text
	version_          = other.version_;
	range_cache_      = {};
	find_cache_       = {};
	last_deleted_     = {};
	other.last_deleted_ = {};
	piece_ends_version_       = std::numeric_limits<std::uint64_t>::max();
	other.piece_ends_version_ = std::numeric_limits<std::uint64_t>::max();
	InvalidateLineIndex();
	return *this;
}


PieceTable::~PieceTable() = default;


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
	std::string().swap(original_);
	add_.clear();
	materialized_.clear();
	total_size_ = 0;
	dirty_      = true;
	line_index_.clear();
	line_index_dirty_ = true;
	version_++;
	range_cache_  = {};
	find_cache_   = {};
	last_deleted_ = {};
}


void
PieceTable::AdoptOriginal(std::string &&bytes)
{
	// Allocate before touching the current content: if this throws, the
	// table is unchanged (everything below is non-throwing).
	std::vector<Piece> pieces;
	if (!bytes.empty())
		pieces.push_back(Piece{Source::Original, 0, bytes.size()});
	Clear();
	std::string().swap(add_); // release a previous content's capacity
	std::string().swap(materialized_);
	original_   = std::move(bytes);
	total_size_ = original_.size();
	pieces_.swap(pieces);
	version_++;
}


std::string_view
PieceTable::ContentView() const
{
	if (total_size_ == 0)
		return {};
	if (pieces_.size() == 1) {
		const Piece &p = pieces_.front();
		return {(p.src == Source::Original ? original_ : add_).data() + p.start, p.len};
	}
	materialize();
	return {materialized_.data(), materialized_.size()};
}


std::string_view
PieceTable::View(std::size_t byte_offset, std::size_t len) const
{
	if (byte_offset >= total_size_ || len == 0)
		return {};
	len = std::min(len, total_size_ - byte_offset);
	if (dirty_) {
		// Materialized text is stale: serve the range from its piece when it
		// fits in one, rather than copying the whole buffer.
		auto [idx, inner] = locate(byte_offset);
		if (idx < pieces_.size() && inner + len <= pieces_[idx].len) {
			const Piece &p = pieces_[idx];
			return {(p.src == Source::Original ? original_ : add_).data() + p.start + inner, len};
		}
	}
	return ContentView().substr(byte_offset, len);
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
				last.len    += len;
				total_size_ += len;
				dirty_      = true;
				InvalidateLineIndex();
				version_++;
				range_cache_ = {};
				find_cache_  = {};
				return;
			}
		}
	}

	pieces_.push_back(Piece{src, start, len});
	total_size_ += len;
	dirty_      = true;
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
			first.len   += len;
			total_size_ += len;
			dirty_      = true;
			InvalidateLineIndex();
			version_++;
			range_cache_ = {};
			find_cache_  = {};
			return;
		}
	}
	pieces_.insert(pieces_.begin(), Piece{src, start, len});
	total_size_ += len;
	dirty_      = true;
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
	// Rebuilt once per modification: an edited buffer's lookups (two per
	// rendered line per frame) used to walk the piece list each time.
	if (piece_ends_version_ != version_ || piece_ends_.size() != pieces_.size()) {
		piece_ends_.resize(pieces_.size());
		std::size_t end = 0;
		for (std::size_t i = 0; i < pieces_.size(); ++i)
			piece_ends_[i] = end += pieces_[i].len;
		piece_ends_version_ = version_;
	}
	const auto it = std::upper_bound(piece_ends_.begin(), piece_ends_.end(), byte_offset);
	if (it == piece_ends_.end())
		return {pieces_.size(), 0}; // inconsistent sizes; treat as the end
	const std::size_t i = static_cast<std::size_t>(it - piece_ends_.begin());
	return {i, byte_offset - (i > 0 ? piece_ends_[i - 1] : 0)};
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


std::size_t
PieceTable::lineUpperBound(const std::size_t byte_offset) const
{
	// Line starts ascend; binary search over them with the shift applied.
	std::size_t lo = 0, hi = line_index_.size();
	while (lo < hi) {
		const std::size_t mid = lo + (hi - lo) / 2;
		if (lineStart(mid) <= byte_offset)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}


void
PieceTable::applyLineShift() const
{
	for (std::size_t i = line_shift_from_; i < line_index_.size(); ++i)
		line_index_[i] += line_shift_by_;
	line_shift_from_ = std::numeric_limits<std::size_t>::max();
	line_shift_by_   = 0;
}


void
PieceTable::lineIndexOnInsert(std::size_t offset, const char *text, std::size_t len) const
{
	if (line_index_dirty_)
		return;
	// Line starts after the insertion point move right; a start exactly at
	// the insertion point stays (the text joins that line).
	const std::size_t first_after = lineUpperBound(offset);
	std::vector<std::size_t> added;
	const char *end = text + len;
	for (const char *p = text; (p = static_cast<const char *>(std::memchr(p, '\n', end - p))) != nullptr; ++p)
		added.push_back(offset + static_cast<std::size_t>(p - text) + 1);
	// The pending shift can absorb this one only if it starts at the same
	// line (the usual case: typing along one line).
	if (line_shift_by_ != 0 && line_shift_from_ != first_after)
		applyLineShift();
	if (!added.empty())
		line_index_.insert(line_index_.begin() + static_cast<std::ptrdiff_t>(first_after), added.begin(),
		                   added.end());
	// The new starts are exact; the ones after them move by len.
	line_shift_from_ = first_after + added.size();
	line_shift_by_ += len;
}


void
PieceTable::lineIndexOnDelete(std::size_t offset, std::size_t len) const
{
	if (line_index_dirty_)
		return;
	// Starts in (offset, offset+len] followed a deleted newline: drop them.
	// Later starts move left.
	const std::size_t lo = lineUpperBound(offset);
	const std::size_t hi = lineUpperBound(offset + len);
	// After the erase the starts from hi on are at lo; a pending shift
	// beginning anywhere in [lo, hi] covers exactly them.
	if (line_shift_by_ != 0 && (line_shift_from_ < lo || line_shift_from_ > hi))
		applyLineShift();
	line_index_.erase(line_index_.begin() + static_cast<std::ptrdiff_t>(lo),
	                  line_index_.begin() + static_cast<std::ptrdiff_t>(hi));
	line_shift_from_ = lo;
	line_shift_by_ -= len;
}


void
PieceTable::RebuildLineIndex() const
{

	if (!line_index_dirty_) {
		return;
	}
	line_index_.clear();
	line_index_.push_back(0);
	line_shift_from_ = std::numeric_limits<std::size_t>::max();
	line_shift_by_   = 0;

	std::size_t pos = 0;
	for (const auto &pc: pieces_) {
		const std::string &src = pc.src == Source::Original ? original_ : add_;
		const char *base       = src.data() + static_cast<std::ptrdiff_t>(pc.start);

		const char *end        = base + pc.len;
		// The next line starts after each newline.
		for (const char *p = base; (p = static_cast<const char *>(std::memchr(p, '\n', end - p))) != nullptr; ++p)
			line_index_.push_back(pos + static_cast<std::size_t>(p - base) + 1);

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

	last_deleted_ = {};
	const std::size_t add_start = add_.size();
	add_.append(text, len);

	if (pieces_.empty()) {
		pieces_.push_back(Piece{Source::Add, add_start, len});
		total_size_ += len;
		dirty_      = true;
		lineIndexOnInsert(byte_offset, add_.data() + add_start, len);
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
		dirty_      = true;
		lineIndexOnInsert(byte_offset, add_.data() + add_start, len);
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
	dirty_      = true;
	lineIndexOnInsert(byte_offset, add_.data() + add_start, len);
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

	// Remember what a large deletion removed, so undo can refer to the
	// stored bytes instead of keeping its own copy (see TakeDeletedSpans).
	last_deleted_ = {};
	if (len >= kCaptureDeletedMin) {
		last_deleted_.spans  = SpansInRange(byte_offset, len);
		last_deleted_.offset = byte_offset;
		last_deleted_.len    = len;
		last_deleted_.valid  = true;
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
			inner     = 0;
			remaining -= take;
			continue;
		}

		// After modifying current idx, next deletion continues at beginning of the next logical region
		inner     = 0;
		remaining -= take;
		if (remaining == 0)
			break;
		// Move to next piece
		idx += 1;
	}

	total_size_ -= len;
	dirty_      = true;
	lineIndexOnDelete(byte_offset, len);
	if (idx < pieces_.size())
		coalesceNeighbors(idx);
	if (idx > 0)
		coalesceNeighbors(idx - 1);
	maybeConsolidate();
	version_++;
	range_cache_ = {};
	find_cache_  = {};
}


// ===== Span access =====

std::vector<TextSpan>
PieceTable::SpansInRange(std::size_t byte_offset, std::size_t len) const
{
	std::vector<TextSpan> out;
	if (byte_offset >= total_size_ || len == 0)
		return out;
	len               = std::min(len, total_size_ - byte_offset);
	auto [idx, inner] = locate(byte_offset);
	while (len > 0 && idx < pieces_.size()) {
		const Piece &p         = pieces_[idx];
		const std::size_t take = std::min(p.len - inner, len);
		out.push_back(TextSpan{p.src == Source::Add, p.start + inner, take});
		len   -= take;
		inner = 0;
		++idx;
	}
	return out;
}


bool
PieceTable::SpansEqual(const std::vector<TextSpan> &spans, std::string_view text) const
{
	std::size_t pos = 0;
	bool equal      = true;
	VisitSpans(spans, [&](const char *data, std::size_t n) {
		if (!equal || pos + n > text.size() || std::memcmp(data, text.data() + pos, n) != 0)
			equal = false;
		pos += n;
	});
	return equal && pos == text.size();
}


void
PieceTable::InsertSpans(std::size_t byte_offset, const std::vector<TextSpan> &spans)
{
	if (byte_offset > total_size_)
		byte_offset = total_size_;
	std::vector<Piece> ins;
	std::size_t len = 0;
	for (const TextSpan &sp: spans) {
		const std::string &src = sp.add ? add_ : original_;
		if (sp.len == 0 || sp.start + sp.len > src.size())
			continue;
		ins.push_back(Piece{sp.add ? Source::Add : Source::Original, sp.start, sp.len});
		len += sp.len;
	}
	if (len == 0)
		return;

	std::size_t at = pieces_.size();
	if (byte_offset < total_size_) {
		auto [idx, inner] = locate(byte_offset);
		if (inner > 0) {
			// Split the piece containing the insertion point.
			const Piece target = pieces_[idx];
			pieces_[idx].len   = inner;
			pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(idx + 1),
			               Piece{target.src, target.start + inner, target.len - inner});
			at = idx + 1;
		} else {
			at = idx;
		}
	}
	pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(at), ins.begin(), ins.end());
	total_size_ += len;
	dirty_      = true;
	std::size_t off = byte_offset;
	for (const Piece &p: ins) {
		const std::string &src = p.src == Source::Add ? add_ : original_;
		lineIndexOnInsert(off, src.data() + p.start, p.len);
		off += p.len;
	}
	coalesceNeighbors(std::min(at + ins.size(), pieces_.size() - 1));
	if (at > 0)
		coalesceNeighbors(at - 1);
	maybeConsolidate();
	version_++;
	range_cache_  = {};
	find_cache_   = {};
	last_deleted_ = {};
}


std::vector<TextSpan>
PieceTable::TakeDeletedSpans(std::size_t byte_offset, std::size_t len)
{
	std::vector<TextSpan> out;
	if (last_deleted_.valid && last_deleted_.offset == byte_offset && last_deleted_.len == len)
		out = std::move(last_deleted_.spans);
	last_deleted_ = {};
	return out;
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

	// total_size_ unchanged. Content and byte offsets are unchanged too, so
	// the line index stays valid (invalidating it here forced a full rescan
	// on every edit once a buffer had many pieces).
	dirty_ = true;
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
	std::size_t start = lineStart(line_num);
	std::size_t end   = (line_num + 1 < line_index_.size()) ? lineStart(line_num + 1) : total_size_;
	return {start, end};
}


std::string
PieceTable::GetLine(std::size_t line_num) const
{
	auto [start, end] = GetLineRange(line_num);
	if (end < start)
		return std::string();
	// Every line but the last ends at a newline (each newline starts an
	// index entry); leave it out.
	if (end > start && line_num + 1 < line_index_.size())
		end -= 1;
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
	const std::size_t ub = lineUpperBound(byte_offset);
	std::size_t row      = ub == 0 ? 0 : ub - 1;
	std::size_t col      = byte_offset - lineStart(row);
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
	std::size_t start = lineStart(row);
	std::size_t end   = (row + 1 < line_index_.size()) ? lineStart(row + 1) : total_size_;
	// Clamp col to the line's length without its newline (every line but
	// the last has one).
	if (end > start && row + 1 < line_index_.size())
		end -= 1;
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
	{
		if (range_cache_.valid && range_cache_.version == version_ &&
		    range_cache_.off == byte_offset && range_cache_.len == len) {
			return range_cache_.data;
		}
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
				inner     = 0;
				idx       += 1;
			} else {
				break;
			}
		}
	}

	// Update cache
	{
		range_cache_.valid   = true;
		range_cache_.version = version_;
		range_cache_.off     = byte_offset;
		range_cache_.len     = len;
		range_cache_.data    = out;
	}
	return out;
}


std::size_t
PieceTable::Find(const std::string &needle, std::size_t start) const
{
	if (needle.empty())
		return start <= total_size_ ? start : std::numeric_limits<std::size_t>::max();
	if (start > total_size_)
		return std::numeric_limits<std::size_t>::max();
	{
		if (find_cache_.valid &&
		    find_cache_.version == version_ &&
		    find_cache_.needle == needle &&
		    find_cache_.start == start) {
			return find_cache_.result;
		}
	}

	materialize();
	std::size_t pos;
	{
		pos = materialized_.find(needle, start);
		if (pos == std::string::npos)
			pos = std::numeric_limits<std::size_t>::max();
		// Update cache
		find_cache_.valid   = true;
		find_cache_.version = version_;
		find_cache_.needle  = needle;
		find_cache_.start   = start;
		find_cache_.result  = pos;
	}
	return pos;
}


void
PieceTable::WriteToStream(std::ostream &out) const
{
	// Stream the content piece-by-piece without forcing full materialization
	// No lock needed for original_ and add_ if they are not being modified.
	// Since this is a const method and kte's piece table isn't modified by multiple threads
	// (only queried), we just iterate pieces_.
	for (const auto &p: pieces_) {
		if (p.len == 0)
			continue;
		const std::string &src = (p.src == Source::Original) ? original_ : add_;
		const char *base       = src.data() + static_cast<std::ptrdiff_t>(p.start);
		out.write(base, static_cast<std::streamsize>(p.len));
	}
}
