#include <algorithm>
#include <cassert>
#include <cstring>

#include "GapBuffer.h"


GapBuffer::GapBuffer() = default;


GapBuffer::GapBuffer(std::size_t initialCapacity)
	: buffer_(nullptr), size_(0), capacity_(0)
{
	if (initialCapacity > 0) {
		Reserve(initialCapacity);
	}
}


GapBuffer::GapBuffer(const GapBuffer &other)
	: buffer_(nullptr), size_(0), capacity_(0)
{
	if (other.capacity_ > 0) {
		Reserve(other.capacity_);
		if (other.size_ > 0) {
			std::memcpy(buffer_, other.buffer_, other.size_);
			size_ = other.size_;
		}
		setTerminator();
	}
}


GapBuffer &
GapBuffer::operator=(const GapBuffer &other)
{
	if (this == &other)
		return *this;
	if (other.capacity_ > capacity_) {
		Reserve(other.capacity_);
	}
	if (other.size_ > 0) {
		std::memcpy(buffer_, other.buffer_, other.size_);
	}
	size_ = other.size_;
	setTerminator();
	return *this;
}


GapBuffer::GapBuffer(GapBuffer &&other) noexcept
	: buffer_(other.buffer_), size_(other.size_), capacity_(other.capacity_)
{
	other.buffer_   = nullptr;
	other.size_     = 0;
	other.capacity_ = 0;
}


GapBuffer &
GapBuffer::operator=(GapBuffer &&other) noexcept
{
	if (this == &other)
		return *this;
	delete[] buffer_;
	buffer_         = other.buffer_;
	size_           = other.size_;
	capacity_       = other.capacity_;
	other.buffer_   = nullptr;
	other.size_     = 0;
	other.capacity_ = 0;
	return *this;
}


GapBuffer::~GapBuffer()
{
	delete[] buffer_;
}


void
GapBuffer::Reserve(const std::size_t newCapacity)
{
	if (newCapacity <= capacity_) [[likely]]
		return;
	// Allocate space for terminator as well
	char *nb = new char[newCapacity + 1];
	if (size_ > 0 && buffer_) {
		std::memcpy(nb, buffer_, size_);
	}
	delete[] buffer_;
	buffer_   = nb;
	capacity_ = newCapacity;
	setTerminator();
}


void
GapBuffer::AppendChar(const char c)
{
	ensureCapacityFor(1);
	buffer_[size_++] = c;
	setTerminator();
}


void
GapBuffer::Append(const char *s, const std::size_t len)
{
	if (!s || len == 0) [[unlikely]]
		return;
	ensureCapacityFor(len);
	std::memcpy(buffer_ + size_, s, len);
	size_ += len;
	setTerminator();
}


void
GapBuffer::Append(const GapBuffer &other)
{
	if (other.size_ == 0)
		return;
	Append(other.buffer_, other.size_);
}


void
GapBuffer::PrependChar(char c)
{
	ensureCapacityFor(1);
	// shift right by 1
	if (size_ > 0) [[likely]] {
		std::memmove(buffer_ + 1, buffer_, size_);
	}
	buffer_[0] = c;
	++size_;
	setTerminator();
}


void
GapBuffer::Prepend(const char *s, std::size_t len)
{
	if (!s || len == 0) [[unlikely]]
		return;
	ensureCapacityFor(len);
	if (size_ > 0) [[likely]] {
		std::memmove(buffer_ + len, buffer_, size_);
	}
	std::memcpy(buffer_, s, len);
	size_ += len;
	setTerminator();
}


void
GapBuffer::Prepend(const GapBuffer &other)
{
	if (other.size_ == 0)
		return;
	Prepend(other.buffer_, other.size_);
}


void
GapBuffer::Clear()
{
	size_ = 0;
	setTerminator();
}


void
GapBuffer::ensureCapacityFor(std::size_t delta)
{
	if (capacity_ - size_ >= delta) [[likely]]
		return;
	auto required = size_ + delta;
	Reserve(growCapacity(capacity_, required));
}


std::size_t
GapBuffer::growCapacity(std::size_t current, std::size_t required)
{
	// geometric growth, at least required
	std::size_t newCap = current ? current : 8;
	while (newCap < required)
		newCap = newCap + (newCap >> 1); // 1.5x growth
	return newCap;
}


void
GapBuffer::setTerminator() const
{
	if (!buffer_) {
		return;
	}

	buffer_[size_] = '\0';
}