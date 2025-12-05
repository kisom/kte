#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <memory>
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <signal.h>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

#include "Command.h"
#include "Editor.h"
#include "Frontend.h"
#include "TerminalFrontend.h"

#if defined(KTE_BUILD_GUI)
#if defined(KTE_USE_QT)
#include "QtFrontend.h"
#else
#include "ImGuiFrontend.h"
#endif
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
		<< "  -V, --version    Show version and exit\n"
		<< "      --stress-highlighter[=SECONDS]  Run a short highlighter stress harness (debug aid)\n";
}


static int
RunStressHighlighter(unsigned seconds)
{
	// Build a synthetic buffer with code-like content
	Buffer buf;
	buf.SetFiletype("cpp");
	buf.SetSyntaxEnabled(true);
	buf.EnsureHighlighter();
	// Seed with many lines
	const int N = 1200;
	for (int i = 0; i < N; ++i) {
		std::string line = "int v" + std::to_string(i) + " = " + std::to_string(i) + "; // line\n";
		buf.insert_row(i, line);
	}
	// Remove the extra last empty row if any artifacts
	// Simulate a viewport of ~60 rows
	const int viewport_rows = 60;
	const auto start_ts     = std::chrono::steady_clock::now();
	std::mt19937 rng{1234567u};
	std::uniform_int_distribution<int> row_d(0, N - 1);
	std::uniform_int_distribution<int> op_d(0, 2);
	std::uniform_int_distribution<int> sleep_d(0, 2);

	// Loop performing edits and highlighter queries while background worker runs
	while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_ts).count() <
	       seconds) {
		int fr = row_d(rng);
		if (fr + viewport_rows >= N)
			fr = std::max(0, N - viewport_rows - 1);
		buf.SetOffsets(static_cast<std::size_t>(fr), 0);
		if (buf.Highlighter()) {
			buf.Highlighter()->PrefetchViewport(buf, fr, viewport_rows, buf.Version());
		}
		// Do a few direct GetLine calls over the viewport to shake the caches
		if (buf.Highlighter()) {
			for (int r = 0; r < viewport_rows; r += 7) {
				(void) buf.Highlighter()->GetLine(buf, fr + r, buf.Version());
			}
		}
		// Random simple edit
		int op = op_d(rng);
		int r  = row_d(rng);
		if (op == 0) {
			buf.insert_text(r, 0, "/*X*/");
			buf.SetDirty(true);
		} else if (op == 1) {
			buf.delete_text(r, 0, 1);
			buf.SetDirty(true);
		} else {
			// split and join occasionally
			buf.split_line(r, 0);
			buf.join_lines(std::min(r + 1, N - 1));
			buf.SetDirty(true);
		}
		// tiny sleep to allow background thread to interleave
		if (sleep_d(rng) == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	return 0;
}


int
main(int argc, const char *argv[])
{
	Editor editor;

	// CLI parsing using getopt_long
	bool req_gui                   = false;
	[[maybe_unused]] bool req_term = false;
	bool show_help                 = false;
	bool show_version              = false;

	static struct option long_opts[] = {
		{"gui", no_argument, nullptr, 'g'},
		{"term", no_argument, nullptr, 't'},
		{"help", no_argument, nullptr, 'h'},
		{"version", no_argument, nullptr, 'V'},
		{"stress-highlighter", optional_argument, nullptr, 1000},
		{nullptr, 0, nullptr, 0}
	};

	int opt;
	int long_index          = 0;
	unsigned stress_seconds = 0;
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
		case 1000: {
			stress_seconds = 5; // default
			if (optarg && *optarg) {
				try {
					unsigned v = static_cast<unsigned>(std::stoul(optarg));
					if (v > 0 && v < 36000)
						stress_seconds = v;
				} catch (...) {}
			}
			break;
		}
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

	if (stress_seconds > 0) {
		return RunStressHighlighter(stress_seconds);
	}

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
							if (v > std::numeric_limits<std::size_t>::max()) {
								std::cerr <<
									"kte: Warning: Line number too large, ignoring\n";
								pending_line = 0;
							} else {
								pending_line = static_cast<std::size_t>(v);
							}
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

#if defined(KTE_BUILD_GUI) && defined(__APPLE__)
	if (use_gui) {
		/* likely using the .app, so need to cd */
		const char *home = getenv("HOME");
		if (!home) {
			std::cerr << "kge.app: HOME environment variable not set" << std::endl;
			return 1;
		}
		if (chdir(home) != 0) {
			std::cerr << "kge.app: failed to chdir to " << home << ": "
				<< std::strerror(errno) << std::endl;
			return 1;
		}
	}
#endif

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