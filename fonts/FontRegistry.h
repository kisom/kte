#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Font.h"

namespace kte::Fonts {
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


	// Request font load to be applied at a safe time (e.g., before starting a new frame)
	// Thread-safe. Frontend should call ConsumePendingFontRequest() and then LoadFont().
	void RequestLoadFont(const std::string &name, float size)
	{
		std::lock_guard lock(mutex_);
		pending_name_ = name;
		pending_size_ = size;
		has_pending_  = true;
	}


	// Retrieve and clear a pending font request. Returns true if there was one.
	bool ConsumePendingFontRequest(std::string &name, float &size)
	{
		std::lock_guard lock(mutex_);
		if (!has_pending_)
			return false;
		name         = pending_name_;
		size         = pending_size_;
		has_pending_ = false;
		return true;
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

	// Pending font change request (applied by frontend between frames)
	bool has_pending_ = false;
	std::string pending_name_;
	float pending_size_ = 0.0f;
};


void InstallDefaultFonts();
}