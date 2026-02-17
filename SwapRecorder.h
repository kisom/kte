// SwapRecorder.h - minimal swap journal recording interface for Buffer mutations
#pragma once

#include <cstddef>
#include <string_view>

namespace kte {
// SwapRecorder is a tiny, non-blocking callback interface.
// Implementations must return quickly; Buffer calls these hooks after a
// mutation succeeds.
class SwapRecorder {
public:
	virtual ~SwapRecorder() = default;

	virtual void OnInsert(int row, int col, std::string_view bytes) = 0;

	virtual void OnDelete(int row, int col, std::size_t len) = 0;
};
} // namespace kte
