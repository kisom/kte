#include "FontRegistry.h"
#include "FontList.h"

namespace kte::Fonts {
void
InstallDefaultFonts()
{
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"default",
		const_cast<unsigned int *>(BrassMono::DefaultFontBoldCompressedData),
		BrassMono::DefaultFontBoldCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"b612",
		const_cast<unsigned int *>(B612Mono::DefaultFontRegularCompressedData),
		B612Mono::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"brassmono",
		const_cast<unsigned int *>(BrassMono::DefaultFontBoldCompressedData),
		BrassMono::DefaultFontBoldCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"brassmonocode",
		const_cast<unsigned int *>(BrassMonoCode::DefaultFontBoldCompressedData),
		BrassMonoCode::DefaultFontBoldCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"fira",
		const_cast<unsigned int *>(FiraCode::DefaultFontRegularCompressedData),
		FiraCode::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"go",
		const_cast<unsigned int *>(Go::DefaultFontRegularCompressedData),
		Go::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"ibm",
		const_cast<unsigned int *>(IBMPlexMono::DefaultFontRegularCompressedData),
		IBMPlexMono::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"idealist",
		const_cast<unsigned int *>(Idealist::DefaultFontRegularCompressedData),
		Idealist::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"inconsolata",
		const_cast<unsigned int *>(Inconsolata::DefaultFontRegularCompressedData),
		Inconsolata::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"inconsolataex",
		const_cast<unsigned int *>(InconsolataExpanded::DefaultFontRegularCompressedData),
		InconsolataExpanded::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"iosevka",
		const_cast<unsigned int *>(Iosoveka::DefaultFontRegularCompressedData),
		Iosoveka::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"iosevkaex",
		const_cast<unsigned int *>(IosevkaExtended::DefaultFontRegularCompressedData),
		IosevkaExtended::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"sharetech",
		const_cast<unsigned int *>(ShareTech::DefaultFontRegularCompressedData),
		ShareTech::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"space",
		const_cast<unsigned int *>(SpaceMono::DefaultFontRegularCompressedData),
		SpaceMono::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"syne",
		const_cast<unsigned int *>(Syne::DefaultFontRegularCompressedData),
		Syne::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"triplicate",
		const_cast<unsigned int *>(Triplicate::DefaultFontRegularCompressedData),
		Triplicate::DefaultFontRegularCompressedSize
	));
	FontRegistry::Instance().Register(std::make_unique<Font>(
		"unispace",
		const_cast<unsigned int *>(Unispace::DefaultFontRegularCompressedData),
		Unispace::DefaultFontRegularCompressedSize
	));
}
}