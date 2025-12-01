#include <iostream>
#include <memory>
#include <string>
#include <cctype>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <cstdio>
#include <sys/stat.h>

#include "Editor.h"
#include "Command.h"
#include "Frontend.h"
#include "TerminalFrontend.h"

#if defined(KTE_BUILD_GUI)
#include "GUIFrontend.h"
#endif


#ifndef KTE_VERSION_STR
#  define KTE_VERSION_STR "devel"
#endif

static void
PrintUsage(const char *prog)
{
	std::cerr << "Usage: " << prog << " [OPTIONS] [files]\n"
		<< "Options:\n"
		<< "  -g, --gui        Use GUI frontend (if built)\n"
		<< "  -t, --term       Use terminal (ncurses) frontend [default]\n"
		<< "  -h, --help       Show this help and exit\n"
		<< "  -V, --version    Show version and exit\n";
}


int
main(int argc, const char *argv[])
{
	Editor editor;

	// CLI parsing using getopt_long
	bool req_gui      = false;
	bool req_term     = false;
	bool show_help    = false;
	bool show_version = false;

	static struct option long_opts[] = {
		{"gui", no_argument, nullptr, 'g'},
		{"term", no_argument, nullptr, 't'},
		{"help", no_argument, nullptr, 'h'},
		{"version", no_argument, nullptr, 'V'},
		{nullptr, 0, nullptr, 0}
	};

	int opt;
	int long_index = 0;
	while ((opt = getopt_long(argc, const_cast<char *const *>(argv), "gthV", long_opts, &long_index)) != -1) {
		switch (opt) {
		case 'g':
			req_gui = true;
			break;
		case 't':
			req_term = true;
			break;
		case 'h':
			show_help = true;
			break;
		case 'V':
			show_version = true;
			break;
		case '?':
		default:
			PrintUsage(argv[0]);
			return 2;
		}
	}

	if (show_help) {
		PrintUsage(argv[0]);
		return 0;
	}
	if (show_version) {
		std::cout << "kte " << KTE_VERSION_STR << "\n";
		return 0;
	}

#if !defined(KTE_BUILD_GUI)
	(void) req_term; // suppress unused warning when GUI is not compiled in
#endif

	// Determine frontend
#if !defined(KTE_BUILD_GUI)
	if (req_gui) {
		std::cerr << "kte: GUI not built. Reconfigure with -DBUILD_GUI=ON and required deps installed." <<
			std::endl;
		return 2;
	}
#else
	bool use_gui = false;
	if (req_gui) {
		use_gui = true;
	} else if (req_term) {
		use_gui = false;
	} else {
		// Default depends on build target: kge defaults to GUI, kte to terminal
#if defined(KTE_DEFAULT_GUI)
		use_gui = true;
#else
		use_gui = false;
#endif
	}
#endif

	// Open files passed on the CLI; support +N to jump to line N in the next file.
	// If no files are provided, create an empty buffer.
	if (optind < argc) {
		std::size_t pending_line = 0; // 0 = no pending line
		for (int i = optind; i < argc; ++i) {
			const char *arg = argv[i];
			if (arg && arg[0] == '+') {
				// Parse +<digits>
				const char *p = arg + 1;
				if (*p != '\0') {
					bool all_digits = true;
					for (const char *q = p; *q; ++q) {
						if (!std::isdigit(static_cast<unsigned char>(*q))) {
							all_digits = false;
							break;
						}
					}
					if (all_digits) {
						// Clamp to >=1 later; 0 disables.
						try {
							unsigned long v = std::stoul(p);
							pending_line    = static_cast<std::size_t>(v);
						} catch (...) {
							// Ignore malformed huge numbers
							pending_line = 0;
						}
						continue; // look for the next file arg
					}
				}
				// Fall through: not a +number, treat as filename starting with '+'
			}

			std::string err;
			const std::string path = arg;
			if (!editor.OpenFile(path, err)) {
				editor.SetStatus("open: " + err);
				std::cerr << "kte: " << err << "\n";
			} else if (pending_line > 0) {
				// Apply pending +N to the just-opened (current) buffer
				if (Buffer *b = editor.CurrentBuffer()) {
					std::size_t nrows = b->Nrows();
					std::size_t line  = pending_line > 0 ? pending_line - 1 : 0;
					// 1-based to 0-based
					if (nrows > 0) {
						if (line >= nrows)
							line = nrows - 1;
					} else {
						line = 0;
					}
					b->SetCursor(0, line);
					// Do not force viewport offsets here; the frontend/renderer
					// will establish dimensions and normalize visibility on first draw.
				}
				pending_line = 0; // consumed
			}
		}
		// If we ended with a pending +N but no subsequent file, ignore it.
	} else {
		// Create a single empty buffer
		editor.AddBuffer(Buffer());
		editor.SetStatus("new: empty buffer");
	}

	// Install built-in commands
	InstallDefaultCommands();

	// Select frontend
	std::unique_ptr<Frontend> fe;
#if defined(KTE_BUILD_GUI)
	if (use_gui) {
		fe = std::make_unique<GUIFrontend>();
	} else
#endif
	{
		fe = std::make_unique<TerminalFrontend>();
	}

	if (!fe->Init(editor)) {
		std::cerr << "kte: failed to initialize frontend" << std::endl;
		return 1;
	}

	bool running = true;
	while (running) {
		fe->Step(editor, running);
	}

	fe->Shutdown();

	return 0;
}
