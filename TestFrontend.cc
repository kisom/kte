#include "TestFrontend.h"
#include "Editor.h"
#include "Command.h"
#include <iostream>


bool
TestFrontend::Init(Editor &ed)
{
	ed.SetDimensions(24, 80);
	return true;
}


void
TestFrontend::Step(Editor &ed, bool &running)
{
	MappedInput mi;
	if (input_.Poll(mi)) {
		if (mi.hasCommand) {
			Execute(ed, mi.id, mi.arg, mi.count);
		}
	}

	if (ed.QuitRequested()) {
		running = false;
	}

	renderer_.Draw(ed);
}


void
TestFrontend::Shutdown() {}
