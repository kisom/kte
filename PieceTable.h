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

	// Underlying storages
	std::string original_; // unused for builder use-case, but kept for API symmetry
	std::string add_;
	std::vector<Piece> pieces_;

	mutable std::string materialized_;
	mutable bool dirty_     = true;
	std::size_t total_size_ = 0;
};