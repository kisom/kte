#include <iostream>
#include <string>
#include <unistd.h>
#include <getopt.h>

#include "Editor.h"
#include "Command.h"
#include "Frontend.h"
#include "TerminalFrontend.h"
#if defined(KTE_BUILD_GUI)
#include "GUIFrontend.h"
#endif


#ifndef KGE_VERSION
#  define KTE_VERSION_STR "dev"
#else
#  define KTE_STR_HELPER(x) #x
#  define KTE_STR(x) KTE_STR_HELPER(x)
#  define KTE_VERSION_STR KTE_STR(KGE_VERSION)
#endif

static void PrintUsage(const char* prog)
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
    bool req_gui = false;
    bool req_term = false;
    bool show_help = false;
    bool show_version = false;

    static struct option long_opts[] = {
            {"gui",     no_argument,       nullptr, 'g'},
            {"term",    no_argument,       nullptr, 't'},
            {"help",    no_argument,       nullptr, 'h'},
            {"version", no_argument,       nullptr, 'V'},
            {nullptr,    0,                 nullptr,  0 }
    };

    int opt;
    int long_index = 0;
    while ((opt = getopt_long(argc, const_cast<char* const*>(argv), "gthV", long_opts, &long_index)) != -1) {
        switch (opt) {
            case 'g': req_gui = true; break;
            case 't': req_term = true; break;
            case 'h': show_help = true; break;
            case 'V': show_version = true; break;
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
    (void)req_term; // suppress unused warning when GUI is not compiled in
#endif

    // Determine frontend
#if !defined(KTE_BUILD_GUI)
    if (req_gui) {
        std::cerr << "kte: GUI not built. Reconfigure with -DBUILD_GUI=ON and required deps installed." << std::endl;
        return 2;
    }
#else
    bool use_gui = false;
    if (req_gui) {
        use_gui = true;
    } else if (req_term) {
        use_gui = false;
    } else {
        // Default to terminal
        use_gui = false;
    }
#endif

    // Open files passed on the CLI; if none, create an empty buffer
    if (optind < argc) {
        for (int i = optind; i < argc; ++i) {
            std::string err;
            const std::string path = argv[i];
            if (!editor.OpenFile(path, err)) {
                editor.SetStatus("open: " + err);
                std::cerr << "kte: " << err << "\n";
            }
        }
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
        fe.reset(new GUIFrontend());
    } else
#endif
    {
        fe.reset(new TerminalFrontend());
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
