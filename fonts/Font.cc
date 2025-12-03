#include "Font.h"

#include "imgui.h"

namespace kte::Fonts {
void
Font::Load(const float size) const
{
	const ImGuiIO &io = ImGui::GetIO();
	io.Fonts->Clear();
	const ImFont *font = io.Fonts->AddFontFromMemoryCompressedTTF(
		this->data_,
		this->size_,
		size);

	if (!font) {
		font = io.Fonts->AddFontDefault();
	}

	(void) font;
	io.Fonts->Build();
}
} // namespace kte::Fonts