#ifndef KTE_UNDONODE_H
#define KTE_UNDONODE_H

#include <cstddef>
#include <cstddef>


enum UndoKind {
	UNDO_INSERT,
	UNDO_DELETE,
};


class UndoNode {
public:
	explicit UndoNode(const UndoKind kind, const size_t row, const size_t col)
		: kind_(kind), row_(row), col_(col) {}


	~UndoNode() = default;


	[[nodiscard]] UndoKind Kind() const
	{
		return kind_;
	}


	[[nodiscard]] UndoNode *Next() const
	{
		return next_;
	}


	void Next(UndoNode *next)
	{
		next_ = next;
	}


	[[nodiscard]] UndoNode *Child() const
	{
		return child_;
	}


	void Child(UndoNode *child)
	{
		child_ = child;
	}


	void SetRowCol(const std::size_t row, const std::size_t col)
	{
		this->row_ = row;
		this->col_ = col;
	}


	[[nodiscard]] std::size_t Row() const
	{
		return row_;
	}


	[[nodiscard]] std::size_t Col() const
	{
		return col_;
	}


	void DeleteNext() const;

private:
	[[maybe_unused]] UndoKind kind_;
	[[maybe_unused]] UndoNode *next_{nullptr};
	[[maybe_unused]] UndoNode *child_{nullptr};

	[[maybe_unused]] std::size_t row_{}, col_{};
};


#endif // KTE_UNDONODE_H
