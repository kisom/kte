#include "Test.h"

#include "Buffer.h"
#include "Command.h"
#include "Editor.h"
#include "UndoSystem.h"

#include <string>


static std::string
to_string_rows(const Buffer &buf)
{
	std::string out;
	for (const auto &r: buf.Rows()) {
		out += static_cast<std::string>(r);
		out.push_back('\n');
	}
	return out;
}


TEST (ReflowUndo)
{
	InstallDefaultCommands();

	Editor ed;
	ed.SetDimensions(24, 80);

	Buffer b;
	const std::string initial =
		"This is a very long line that should be reflowed into multiple lines to see if undo works correctly.\n";
	b.insert_text(0, 0, initial);
	b.SetCursor(0, 0);

	// Commit initial insertion so it's its own undo step
	if (auto *u = b.Undo())
		u->commit();

	ed.AddBuffer(std::move(b));

	Buffer *buf = ed.CurrentBuffer();
	ASSERT_TRUE(buf != nullptr);

	const std::string original_dump = to_string_rows(*buf);

	// Reflow with small width
	const int width = 20;
	ASSERT_TRUE(Execute(ed, "reflow-paragraph", "", width));

	const std::string reflowed_dump = to_string_rows(*buf);
	ASSERT_TRUE(reflowed_dump != original_dump);
	ASSERT_TRUE(buf->Rows().size() > 1);

	// Undo reflow
	ASSERT_TRUE(Execute(ed, "undo", "", 1));
	const std::string after_undo_dump = to_string_rows(*buf);

	if (after_undo_dump != original_dump) {
		fprintf(stderr, "Undo failed.\nExpected:\n%s\nGot:\n%s\n", original_dump.c_str(),
		        after_undo_dump.c_str());
	}
	EXPECT_TRUE(after_undo_dump == original_dump);

	// Redo reflow
	ASSERT_TRUE(Execute(ed, "redo", "", 1));
	const std::string after_redo_dump = to_string_rows(*buf);
	EXPECT_TRUE(after_redo_dump == reflowed_dump);
}