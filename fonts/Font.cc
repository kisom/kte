#include "Font.h"
#include "IosevkaExtended.h"

#include "imgui.h"

namespace kte::Fonts {
void
Font::Load(const float size) const
{
	const ImGuiIO &io = ImGui::GetIO();
	io.Fonts->Clear();

	ImFontConfig config;
	config.MergeMode = false;

	// Load Basic Latin + Latin Supplement
	io.Fonts->AddFontFromMemoryCompressedTTF(
		this->data_,
		this->size_,
		size,
		&config,
		io.Fonts->GetGlyphRangesDefault());

	// Merge Greek and Mathematical symbols from IosevkaExtended as fallback
	config.MergeMode                       = true;
	static const ImWchar extended_ranges[] = {
		0x0370, 0x03FF, // Greek and Coptic
		0x2200, 0x22FF, // Mathematical Operators
		0,
	};
	io.Fonts->AddFontFromMemoryCompressedTTF(
		kte::Fonts::IosevkaExtended::DefaultFontRegularCompressedData,
		kte::Fonts::IosevkaExtended::DefaultFontRegularCompressedSize,
		size,
		&config,
		extended_ranges);

	io.Fonts->Build();
}
} // namespace kte::Fonts