cmake_minimum_required(VERSION 3.15)
project(kte)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(KTE_VERSION "1.3.5")

# Default to terminal-only build to avoid SDL/OpenGL dependency by default.
# Enable with -DBUILD_GUI=ON when SDL2/OpenGL/Freetype are available.
set(BUILD_GUI ON CACHE BOOL "Enable building the graphical version.")
set(BUILD_TESTS OFF CACHE BOOL "Enable building test programs.")
option(KTE_USE_PIECE_TABLE "Use PieceTable instead of GapBuffer implementation" ON)
set(KTE_FONT_SIZE "18.0" CACHE STRING "Default font size for GUI")
option(KTE_UNDO_DEBUG "Enable undo instrumentation logs" OFF)
option(KTE_ENABLE_TREESITTER "Enable optional Tree-sitter highlighter adapter" OFF)

# Optionally enable AddressSanitizer (ASan)
option(ENABLE_ASAN "Enable AddressSanitizer for builds" ON)

if (ENABLE_ASAN)
    message(STATUS "ASan enabled")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    # Ensure the sanitizer is linked too (especially important on some platforms)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif ()

if (CMAKE_HOST_UNIX)
    message(STATUS "Build system is POSIX.")
else ()
    message(STATUS "Build system is NOT POSIX.")
endif ()

add_compile_options(
        "-static"
        "-Wall"
        "-Wextra"
        "-Werror"
        "-Wno-unused-function"
        "-Wno-unused-parameter"
        "-g"
        "$<$<CONFIG:RELEASE>:-O2>"
)

if (MSVC)
    add_compile_options("/W4" "$<$<CONFIG:RELEASE>:/O2>")
else ()
    add_compile_options(
            "-Wall"
            "-Wextra"
            "-Werror"
            "$<$<CONFIG:DEBUG>:-g>"
            "$<$<CONFIG:RELEASE>:-O2>")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options("-stdlib=libc++")
    else ()
        # nothing special for gcc at the moment
    endif ()
endif ()
add_compile_definitions(KGE_PLATFORM=${CMAKE_HOST_SYSTEM_NAME})
add_compile_definitions(KTE_VERSION_STR="v${KTE_VERSION}")
if (KTE_ENABLE_TREESITTER)
    add_compile_definitions(KTE_ENABLE_TREESITTER)
endif ()

message(STATUS "Build system: ${CMAKE_HOST_SYSTEM_NAME}")

if (${BUILD_GUI})
    include(cmake/imgui.cmake)
endif ()

# NCurses for terminal mode
set(CURSES_NEED_NCURSES)
set(CURSES_NEED_WIDE)
find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})

set(SYNTAX_SOURCES
        syntax/GoHighlighter.cc
        syntax/CppHighlighter.cc
        syntax/JsonHighlighter.cc
        syntax/ErlangHighlighter.cc
        syntax/MarkdownHighlighter.cc
        syntax/TreeSitterHighlighter.cc
        syntax/LispHighlighter.cc
        syntax/HighlighterEngine.cc
        syntax/RustHighlighter.cc
        syntax/HighlighterRegistry.cc
        syntax/SqlHighlighter.cc
        syntax/NullHighlighter.cc
        syntax/ForthHighlighter.cc
        syntax/PythonHighlighter.cc
        syntax/ShellHighlighter.cc
)

if (KTE_ENABLE_TREESITTER)
    list(APPEND SYNTAX_SOURCES
            TreeSitterHighlighter.cc)
endif ()

set(FONT_SOURCES
        fonts/Font.cc
        fonts/FontRegistry.cc
)

set(GUI_SOURCES
        ${FONT_SOURCES}
        GUIConfig.cc
        GUIRenderer.cc
        GUIInputHandler.cc
        GUIFrontend.cc
)


set(COMMON_SOURCES
        GapBuffer.cc
        PieceTable.cc
        Buffer.cc
        Editor.cc
        Command.cc
        HelpText.cc
        KKeymap.cc
        TerminalInputHandler.cc
        TerminalRenderer.cc
        TerminalFrontend.cc
        TestInputHandler.cc
        TestRenderer.cc
        TestFrontend.cc
        UndoNode.cc
        UndoTree.cc
        UndoSystem.cc

        ${SYNTAX_SOURCES}
)

set(SYNTAX_HEADERS
        syntax/GoHighlighter.h
        syntax/HighlighterEngine.h
        syntax/ShellHighlighter.h
        syntax/MarkdownHighlighter.h
        syntax/LispHighlighter.h
        syntax/SqlHighlighter.h
        syntax/ForthHighlighter.h
        syntax/JsonHighlighter.h
        syntax/TreeSitterHighlighter.h
        syntax/NullHighlighter.h
        syntax/CppHighlighter.h
        syntax/ErlangHighlighter.h
        syntax/LanguageHighlighter.h
        syntax/RustHighlighter.h
        syntax/PythonHighlighter.h
)

if (KTE_ENABLE_TREESITTER)
    list(APPEND THEME_HEADERS
            TreeSitterHighlighter.h)
endif ()

set(THEME_HEADERS
        themes/ThemeHelpers.h
        themes/EInk.h
        themes/Gruvbox.h
        themes/Solarized.h
        themes/Plan9.h
        themes/Nord.h
        themes/Everforest.h
        themes/KanagawaPaper.h
        themes/LCARS.h
        themes/OldBook.h
        themes/Amber.h
        themes/Orbital.h
        themes/WeylandYutani.h
        themes/Zenburn.h
)

set(FONT_HEADERS
        fonts/Font.h
        fonts/FontRegistry.h
        fonts/FontRegistry.h
        fonts/FontList.h
        fonts/B612Mono.h
        fonts/BrassMono.h
        fonts/BrassMonoCode.h
        fonts/FiraCode.h
        fonts/Go.h
        fonts/IBMPlexMono.h
        fonts/Idealist.h
        fonts/Inconsolata.h
        fonts/InconsolataExpanded.h
        fonts/Iosevka.h
        fonts/IosevkaExtended.h
        fonts/ShareTech.h
        fonts/SpaceMono.h
        fonts/Syne.h
        fonts/Triplicate.h
        fonts/Unispace.h
)

set(COMMON_HEADERS
        GapBuffer.h
        PieceTable.h
        Buffer.h
        Editor.h
        AppendBuffer.h
        Command.h
        HelpText.h
        KKeymap.h
        InputHandler.h
        TerminalInputHandler.h
        Renderer.h
        TerminalRenderer.h
        Frontend.h
        TerminalFrontend.h
        TestInputHandler.h
        TestRenderer.h
        TestFrontend.h
        UndoNode.h
        UndoTree.h
        UndoSystem.h
        Highlight.h

        ${SYNTAX_HEADERS}
)

set(GUI_HEADERS
        ${THEME_HEADERS}
        ${FONT_HEADERS}
        GUIConfig.h
        GUIRenderer.h
        GUIInputHandler.h
        GUIFrontend.h
)

# kte (terminal-first) executable
add_executable(kte
        main.cc
        ${COMMON_SOURCES}
        ${COMMON_HEADERS}
)

if (KTE_USE_PIECE_TABLE)
    target_compile_definitions(kte PRIVATE KTE_USE_PIECE_TABLE=1)
endif ()
if (KTE_UNDO_DEBUG)
    target_compile_definitions(kte PRIVATE KTE_UNDO_DEBUG=1)
endif ()

target_link_libraries(kte ${CURSES_LIBRARIES})

if (KTE_ENABLE_TREESITTER)
    # Users can provide their own tree-sitter include/lib via cache variables
    set(TREESITTER_INCLUDE_DIR "" CACHE PATH "Path to tree-sitter include directory")
    set(TREESITTER_LIBRARY "" CACHE FILEPATH "Path to tree-sitter library (.a/.dylib)")
    if (TREESITTER_INCLUDE_DIR)
        target_include_directories(kte PRIVATE ${TREESITTER_INCLUDE_DIR})
    endif ()
    if (TREESITTER_LIBRARY)
        target_link_libraries(kte ${TREESITTER_LIBRARY})
    endif ()
endif ()

install(TARGETS kte
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Man pages
install(FILES docs/kte.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if (BUILD_TESTS)
    # test_undo executable for testing undo/redo system
    add_executable(test_undo
            test_undo.cc
            ${COMMON_SOURCES}
            ${COMMON_HEADERS}
    )

    if (KTE_USE_PIECE_TABLE)
        target_compile_definitions(test_undo PRIVATE KTE_USE_PIECE_TABLE=1)
    endif ()

    if (KTE_UNDO_DEBUG)
        target_compile_definitions(test_undo PRIVATE KTE_UNDO_DEBUG=1)
    endif ()


    target_link_libraries(test_undo ${CURSES_LIBRARIES})
    if (KTE_ENABLE_TREESITTER)
        if (TREESITTER_INCLUDE_DIR)
            target_include_directories(test_undo PRIVATE ${TREESITTER_INCLUDE_DIR})
        endif ()
        if (TREESITTER_LIBRARY)
            target_link_libraries(test_undo ${TREESITTER_LIBRARY})
        endif ()
    endif ()
endif ()

if (${BUILD_GUI})
    # ImGui::CreateContext();
    # ImGuiIO& io = ImGui::GetIO();

    # // Set custom ini filename path to ~/.config/kte/imgui.ini
    # if (const char* home = std::getenv("HOME")) {
    #     static std::string ini_path = std::string(home) + "/.config/kte/imgui.ini";
    #     io.IniFilename = ini_path.c_str();
    # }

    # io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    # io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    # Do not enable GUI in the terminal-first 'kte' binary; GUI is built as separate 'kge'.
    # This avoids referencing GUI classes from kte and keeps dependencies minimal.

    # kge (GUI-first) executable
    add_executable(kge
            main.cc
            ${COMMON_SOURCES}
            ${GUI_SOURCES}
            ${COMMON_HEADERS}
            ${GUI_HEADERS}

    )
    target_compile_definitions(kge PRIVATE KTE_BUILD_GUI=1 KTE_DEFAULT_GUI=1 KTE_FONT_SIZE=${KTE_FONT_SIZE})
    if (KTE_UNDO_DEBUG)
        target_compile_definitions(kge PRIVATE KTE_UNDO_DEBUG=1)
    endif ()
    target_link_libraries(kge ${CURSES_LIBRARIES} imgui)

    # On macOS, build kge as a proper .app bundle
    if (APPLE)
        # Define the icon file
        set(MACOSX_BUNDLE_ICON_FILE kge.icns)
        set(kge_ICON "${CMAKE_CURRENT_SOURCE_DIR}/${MACOSX_BUNDLE_ICON_FILE}")

        # Add icon to the target sources and mark it as a resource
        target_sources(kge PRIVATE ${kge_ICON})
        set_source_files_properties(${kge_ICON} PROPERTIES
                MACOSX_PACKAGE_LOCATION Resources)

        # Configure Info.plist with version and identifiers
        set(KGE_BUNDLE_ID "dev.wntrmute.kge")
        configure_file(
                ${CMAKE_CURRENT_LIST_DIR}/cmake/Info.plist.in
                ${CMAKE_CURRENT_BINARY_DIR}/kge-Info.plist
                @ONLY)

        set_target_properties(kge PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_GUI_IDENTIFIER ${KGE_BUNDLE_ID}
                MACOSX_BUNDLE_BUNDLE_NAME "kge"
                MACOSX_BUNDLE_ICON_FILE ${MACOSX_BUNDLE_ICON_FILE}
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/kge-Info.plist")

        add_dependencies(kge kte)
        add_custom_command(TARGET kge POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:kte>
                $<TARGET_FILE_DIR:kge>/kte
                COMMENT "Copying kte binary into kge.app bundle")

        install(TARGETS kge
                BUNDLE DESTINATION .
        )

        install(TARGETS kte
                RUNTIME DESTINATION kge.app/Contents/MacOS
        )
    else ()
        install(TARGETS kge
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif ()
    # Install kge man page only when GUI is built
    install(FILES docs/kge.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    install(FILES kge.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons)
endif ()
