#pragma once

#include <string>
#include <utility>

#include "BrassMonoCode.h"

namespace kte::Fonts {
// Provide default embedded font aliases used by GUIFrontend fallback loader
inline const unsigned int DefaultFontSize  = BrassMonoCode::DefaultFontBoldCompressedSize;
inline const unsigned int *DefaultFontData = BrassMonoCode::DefaultFontBoldCompressedData;

class Font {
public:
	Font(std::string name, const unsigned int *data, const unsigned int size)
		: name_(std::move(name)),
		  data_(data),
		  size_(size) {}


	std::string Name()
	{
		return name_;
	}


	void Load(float size) const;

private:
	std::string name_;
	const unsigned int *data_{nullptr};
	unsigned int size_{0};
};
}