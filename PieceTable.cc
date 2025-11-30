#include <algorithm>
#include <utility>

#include "PieceTable.h"


PieceTable::PieceTable() = default;


PieceTable::PieceTable(const std::size_t initialCapacity)
{
	add_.reserve(initialCapacity);
	materialized_.reserve(initialCapacity);
}


PieceTable::PieceTable(const PieceTable &other)
	: original_(other.original_),
	  add_(other.add_),
	  pieces_(other.pieces_),
	  materialized_(other.materialized_),
	  dirty_(other.dirty_),
	  total_size_(other.total_size_) {}


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
	return *this;
}


PieceTable::~PieceTable() = default;


void
PieceTable::Reserve(const std::size_t newCapacity)
{
	add_.reserve(newCapacity);
	materialized_.reserve(newCapacity);
}


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
				return;
			}
		}
	}

	pieces_.push_back(Piece{src, start, len});
	total_size_ += len;
	dirty_ = true;
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
			return;
		}
	}
	pieces_.insert(pieces_.begin(), Piece{src, start, len});
	total_size_ += len;
	dirty_ = true;
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
