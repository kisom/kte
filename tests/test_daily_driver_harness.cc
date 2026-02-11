#include "Test.h"

#include "tests/TestHarness.h"


TEST (DailyDriverHarness_Smoke_CanCreateBufferAndInsertText)
{
	ktet::TestHarness h;

	ASSERT_TRUE(h.InsertText("hello"));
	ASSERT_EQ(h.Line(0), std::string("hello"));
}