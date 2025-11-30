#include <iostream>

#include "Buffer.h"

// TIP To <b>Run</b> code, press <shortcut actionId="Run"/> or click the <icon src="AllIcons.Actions.Execute"/> icon in the gutter.
int main(const int argc, const char *argv[])
{
	const auto buffers = new std::vector<Buffer>();

	for (int i = 1; i < argc; i++) {
		auto buffer = Buffer(argv[i]);
		if (i % 2 == 0) {
			buffer.SetDirty(true);
		}

		buffers->emplace_back(buffer);
	}

	std::cout << buffers->size() << " files loaded.\n";
	for (const auto &buffer : *buffers) {
		std::cout << buffer.AsString() << "\n";
	}

	delete buffers;
	return 0;
}