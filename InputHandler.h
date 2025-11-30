/*
 * InputHandler.h - input abstraction and mapping to commands
 */
#ifndef KTE_INPUT_HANDLER_H
#define KTE_INPUT_HANDLER_H

#include <string>

#include "Command.h"

// Result of translating raw input into an editor command.
struct MappedInput {
	bool hasCommand = false;
	CommandId id    = CommandId::Refresh;
	std::string arg; // optional argument (e.g., text for InsertText)
	int count = 0; // optional repeat (C-u not yet implemented)
};

class InputHandler {
public:
	virtual ~InputHandler() = default;

	// Poll for input and translate it to a command. Non-blocking.
	// Returns true if a command is available in 'out'. Returns false if no input.
	virtual bool Poll(MappedInput &out) = 0;
};

#endif // KTE_INPUT_HANDLER_H
