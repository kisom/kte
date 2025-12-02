/*
 * BufferChangeTracker.cc - minimal initial implementation
 */
#include "BufferChangeTracker.h"
#include "../Buffer.h"

namespace kte::lsp {
BufferChangeTracker::BufferChangeTracker(const Buffer *buffer)
	: buffer_(buffer) {}


void
BufferChangeTracker::recordInsertion(int /*row*/, int /*col*/, const std::string &/*text*/)
{
	// For Phase 1–2 bring-up, coalesce to full-document changes
	fullChangePending_ = true;
	++version_;
}


void
BufferChangeTracker::recordDeletion(int /*row*/, int /*col*/, std::size_t /*len*/)
{
	fullChangePending_ = true;
	++version_;
}


std::vector<TextDocumentContentChangeEvent>
BufferChangeTracker::getChanges() const
{
	std::vector<TextDocumentContentChangeEvent> v;
	if (!buffer_)
		return v;
	if (fullChangePending_) {
		TextDocumentContentChangeEvent ev;
		ev.text = buffer_->FullText();
		v.push_back(std::move(ev));
	}
	return v;
}


void
BufferChangeTracker::clearChanges()
{
	fullChangePending_ = false;
}
} // namespace kte::lsp