/*
 * GapBuffer.h - C++ replacement for abuf append/prepend buffer utilities
 */
#ifndef KTE_GAPBUFFER_H
#define KTE_GAPBUFFER_H

#include <cstddef>

class GapBuffer {
public:
	GapBuffer();

	explicit GapBuffer(std::size_t initialCapacity);

	GapBuffer(const GapBuffer &other);

	GapBuffer &operator=(const GapBuffer &other);

	GapBuffer(GapBuffer &&other) noexcept;

	GapBuffer &operator=(GapBuffer &&other) noexcept;

	~GapBuffer();

	void Reserve(std::size_t newCapacity);


	void AppendChar(char c);

	void Append(const char *s, std::size_t len);

	void Append(const GapBuffer &other);

	void PrependChar(char c);

	void Prepend(const char *s, std::size_t len);

	void Prepend(const GapBuffer &other);

	// Content management
	void Clear();

	// Accessors
	char *Data()
	{
		return buffer_;
	}


	[[nodiscard]] const char *Data() const
	{
		return buffer_;
	}


	[[nodiscard]] std::size_t Size() const
	{
		return size_;
	}


	[[nodiscard]] std::size_t Capacity() const
	{
		return capacity_;
	}

private:
	void ensureCapacityFor(std::size_t delta);

	static std::size_t growCapacity(std::size_t current, std::size_t required);

	void setTerminator() const;

	char *buffer_         = nullptr;
	std::size_t size_     = 0; // number of valid bytes (excluding terminator)
	std::size_t capacity_ = 0; // capacity of buffer_ excluding space for terminator
};

#endif // KTE_GAPBUFFER_H
