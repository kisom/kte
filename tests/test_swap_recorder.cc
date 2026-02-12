#include "Test.h"

#include "Buffer.h"
#include "SwapRecorder.h"

#include <string>
#include <vector>

namespace {
struct SwapEvent {
	enum class Type {
		Insert,
		Delete,
	};

	Type type;
	int row;
	int col;
	std::string bytes;
	std::size_t len = 0;
};

class FakeSwapRecorder final : public kte::SwapRecorder {
public:
	std::vector<SwapEvent> events;


	void OnInsert(int row, int col, std::string_view bytes) override
	{
		SwapEvent e;
		e.type  = SwapEvent::Type::Insert;
		e.row   = row;
		e.col   = col;
		e.bytes = std::string(bytes);
		e.len   = 0;
		events.push_back(std::move(e));
	}


	void OnDelete(int row, int col, std::size_t len) override
	{
		SwapEvent e;
		e.type = SwapEvent::Type::Delete;
		e.row  = row;
		e.col  = col;
		e.len  = len;
		events.push_back(std::move(e));
	}
};
} // namespace


TEST (SwapRecorder_InsertABC)
{
	Buffer b;
	FakeSwapRecorder rec;
	b.SetSwapRecorder(&rec);

	b.insert_text(0, 0, std::string_view("abc"));

	ASSERT_EQ(rec.events.size(), (std::size_t) 1);
	ASSERT_TRUE(rec.events[0].type == SwapEvent::Type::Insert);
	ASSERT_EQ(rec.events[0].row, 0);
	ASSERT_EQ(rec.events[0].col, 0);
	ASSERT_EQ(rec.events[0].bytes, std::string("abc"));
}


TEST (SwapRecorder_InsertNewline)
{
	Buffer b;
	FakeSwapRecorder rec;
	b.SetSwapRecorder(&rec);

	b.split_line(0, 0);

	ASSERT_EQ(rec.events.size(), (std::size_t) 1);
	ASSERT_TRUE(rec.events[0].type == SwapEvent::Type::Insert);
	ASSERT_EQ(rec.events[0].row, 0);
	ASSERT_EQ(rec.events[0].col, 0);
	ASSERT_EQ(rec.events[0].bytes, std::string("\n"));
}


TEST (SwapRecorder_DeleteSpanningNewline)
{
	Buffer b;
	// Prepare content without a recorder (should be no-op)
	b.insert_text(0, 0, std::string_view("ab"));
	b.split_line(0, 2);
	b.insert_text(1, 0, std::string_view("cd"));

	FakeSwapRecorder rec;
	b.SetSwapRecorder(&rec);

	// Delete "b\n c" (3 bytes) starting at row 0, col 1.
	b.delete_text(0, 1, 3);

	ASSERT_EQ(rec.events.size(), (std::size_t) 1);
	ASSERT_TRUE(rec.events[0].type == SwapEvent::Type::Delete);
	ASSERT_EQ(rec.events[0].row, 0);
	ASSERT_EQ(rec.events[0].col, 1);
	ASSERT_EQ(rec.events[0].len, (std::size_t) 3);
}