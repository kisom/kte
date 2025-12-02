#ifndef KTE_THEMEHELPERS_H
#define KTE_THEMEHELPERS_H

#include "imgui.h"

// Small helper to convert packed RGB (0xRRGGBB) + optional alpha to ImVec4
static ImVec4
RGBA(const unsigned int rgb, float a = 1.0f)
{
	const float r = static_cast<float>(rgb >> 16 & 0xFF) / 255.0f;
	const float g = static_cast<float>(rgb >> 8 & 0xFF) / 255.0f;
	const float b = static_cast<float>(rgb & 0xFF) / 255.0f;
	return {r, g, b, a};
}


#endif //KTE_THEMEHELPERS_H