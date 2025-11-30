#include "TestInputHandler.h"


bool
TestInputHandler::Poll(MappedInput &out)
{
	if (queue_.empty())
		return false;
	out = queue_.front();
	queue_.pop();
	return true;
}


void
TestInputHandler::QueueCommand(CommandId id, const std::string &arg, int count)
{
	MappedInput mi;
	mi.hasCommand = true;
	mi.id         = id;
	mi.arg        = arg;
	mi.count      = count;
	queue_.push(mi);
}


void
TestInputHandler::QueueText(const std::string &text)
{
	for (char ch: text) {
		MappedInput mi;
		mi.hasCommand = true;
		mi.id         = CommandId::InsertText;
		mi.arg        = std::string(1, ch);
		mi.count      = 0;
		queue_.push(mi);
	}
}
