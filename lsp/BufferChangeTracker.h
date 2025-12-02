/*
 * BufferChangeTracker.h - integrates with Buffer to accumulate LSP-friendly changes
 */
#ifndef KTE_BUFFER_CHANGE_TRACKER_H
#define KTE_BUFFER_CHANGE_TRACKER_H

#include <memory>
#include <vector>
#include <string>

#include "LspTypes.h"

class Buffer; // forward declare from core

namespace kte::lsp {
class BufferChangeTracker {
public:
	explicit BufferChangeTracker(const Buffer *buffer);

	// Called by Buffer on each edit operation
	void recordInsertion(int row, int col, const std::string &text);

	void recordDeletion(int row, int col, std::size_t len);

	// Get accumulated changes since last sync
	std::vector<TextDocumentContentChangeEvent> getChanges() const;

	// Clear changes after sending to LSP
	void clearChanges();

	// Get current document version for LSP
	int getVersion() const
	{
		return version_;
	}

private:
	const Buffer *buffer_   = nullptr;
	bool fullChangePending_ = false;
	int version_            = 0;
};
} // namespace kte::lsp

#endif // KTE_BUFFER_CHANGE_TRACKER_H