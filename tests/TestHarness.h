// TestHarness.h - small helper layer for driving kte headlessly in tests
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "Buffer.h"
#include "Command.h"
#include "Editor.h"

namespace ktet {
inline void
InstallDefaultCommandsOnce()
{
	static bool installed = false;
	if (!installed) {
		InstallDefaultCommands();
		installed = true;
	}
}


class TestHarness {
public:
	TestHarness()
	{
		InstallDefaultCommandsOnce();
		editor_.SetDimensions(24, 80);

		Buffer b;
		b.SetVirtualName("+TEST+");
		editor_.AddBuffer(std::move(b));
	}


	Editor &
	EditorRef()
	{
		return editor_;
	}


	Buffer &
	Buf()
	{
		return *editor_.CurrentBuffer();
	}


	[[nodiscard]] const Buffer &
	Buf() const
	{
		return *editor_.CurrentBuffer();
	}


	bool
	Exec(CommandId id, const std::string &arg = std::string(), int ucount = 0)
	{
		if (ucount > 0) {
			editor_.SetUniversalArg(1, ucount);
		} else {
			editor_.UArgClear();
		}
		return Execute(editor_, id, arg);
	}


	bool
	InsertText(std::string_view text)
	{
		if (text.find('\n') != std::string_view::npos || text.find('\r') != std::string_view::npos)
			return false;
		return Exec(CommandId::InsertText, std::string(text));
	}


	void
	TypeText(std::string_view text)
	{
		for (char ch: text) {
			if (ch == '\n') {
				Exec(CommandId::Newline);
			} else if (ch == '\r') {
				// ignore
			} else {
				Exec(CommandId::InsertText, std::string(1, ch));
			}
		}
	}


	[[nodiscard]] std::string
	Text() const
	{
		const auto &rows = Buf().Rows();
		std::string out;
		for (std::size_t i = 0; i < rows.size(); ++i) {
			out += static_cast<std::string>(rows[i]);
			if (i + 1 < rows.size())
				out.push_back('\n');
		}
		return out;
	}


	[[nodiscard]] std::string
	Line(std::size_t y) const
	{
		return Buf().GetLineString(y);
	}


	bool
	SaveAs(const std::string &path, std::string &err)
	{
		return Buf().SaveAs(path, err);
	}


	bool
	Undo(int ucount = 0)
	{
		return Exec(CommandId::Undo, std::string(), ucount);
	}


	bool
	Redo(int ucount = 0)
	{
		return Exec(CommandId::Redo, std::string(), ucount);
	}

private:
	Editor editor_;
};
} // namespace ktet
