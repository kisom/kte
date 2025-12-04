/*
 * InputHandler.h - input abstraction and mapping to commands
 */
#pragma once
#include <string>

#include "Command.h"

class Editor; // fwd decl


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

	// Optional: attach current Editor so handlers can consult editor state (e.g., universal argument)
	// Default implementation does nothing.
	virtual void Attach(Editor *) {}

	// Poll for input and translate it to a command. Non-blocking.
	// Returns true if a command is available in 'out'. Returns false if no input.
	virtual bool Poll(MappedInput &out) = 0;
};