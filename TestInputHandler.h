/*
 * TestInputHandler.h - programmable input handler for testing
 */
#pragma once
#include <queue>

#include "InputHandler.h"


class TestInputHandler final : public InputHandler {
public:
	TestInputHandler() = default;

	~TestInputHandler() override = default;

	bool Poll(MappedInput &out) override;

	void QueueCommand(CommandId id, const std::string &arg = "", int count = 0);

	void QueueText(const std::string &text);


	[[nodiscard]] bool IsEmpty() const
	{
		return queue_.empty();
	}

private:
	std::queue<MappedInput> queue_;
};