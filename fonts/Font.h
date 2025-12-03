#pragma once

#include <string>
#include <utility>

#include "BrassMonoCode.h"

namespace kte::Fonts {
static const unsigned int DefaultFontSize  = DefaultFontBoldCompressedSize;
static const unsigned int *DefaultFontData = DefaultFontBoldCompressedData;
}

class Font {
public:
	Font(std::string name, unsigned int *data, const unsigned int size)
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
	unsigned int *data_{nullptr};
	unsigned int size_{0};
};