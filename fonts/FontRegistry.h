#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Font.h"

// Forward declaration
// class Font;

class FontRegistry {
public:
	// Get the global instance
	static FontRegistry &Instance()
	{
		static FontRegistry instance;
		return instance;
	}


	// Register a font (usually done at startup or static initialization)
	void Register(std::unique_ptr<Font> font)
	{
		std::lock_guard lock(mutex_);
		auto name = font->Name();
		assert(fonts_.find(name) == fonts_.end() && "Font already registered!");
		fonts_[std::move(name)] = std::move(font);
	}


	// Get a font by name (const access)
	const Font *Get(const std::string &name) const
	{
		std::lock_guard lock(mutex_);
		const auto it = fonts_.find(name);
		return it != fonts_.end() ? it->second.get() : nullptr;
	}


	// Convenience: load a font by name and size
	bool LoadFont(const std::string &name, const float size) const
	{
		if (auto *font = Get(name)) {
			font->Load(size);
			return true;
		}
		return false;
	}


	// Check if font exists
	bool HasFont(const std::string &name) const
	{
		std::lock_guard lock(mutex_);
		return fonts_.count(name) > 0;
	}

private:
	FontRegistry() = default;

	mutable std::mutex mutex_;
	std::unordered_map<std::string, std::unique_ptr<Font> > fonts_;
};