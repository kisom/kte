/*
 * TestInputHandler.h - programmable input handler for testing
 */
#ifndef KTE_TEST_INPUT_HANDLER_H
#define KTE_TEST_INPUT_HANDLER_H

#include "InputHandler.h"
#include <queue>


class TestInputHandler : public InputHandler {
public:
	TestInputHandler() = default;

	~TestInputHandler() override = default;

	bool Poll(MappedInput &out) override;

	void QueueCommand(CommandId id, const std::string &arg = "", int count = 0);

	void QueueText(const std::string &text);


	bool IsEmpty() const
	{
		return queue_.empty();
	}

private:
	std::queue<MappedInput> queue_;
};

#endif // KTE_TEST_INPUT_HANDLER_H
