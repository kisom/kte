cmake_minimum_required(VERSION 3.15)
project(kte)

set(CMAKE_CXX_STANDARD 17)
set(KTE_VERSION "0.0.1")

# Default to terminal-only build to avoid SDL/OpenGL dependency by default.
# Enable with -DBUILD_GUI=ON when SDL2/OpenGL/Freetype are available.
set(BUILD_GUI OFF CACHE BOOL "Enable building the graphical version.")
option(KTE_USE_PIECE_TABLE "Use PieceTable instead of GapBuffer implementation" OFF)

# GUI font configuration: use embedded font from Font.h exclusively for now.
# Font selection via external paths is disabled until revisited later.


if (CMAKE_HOST_UNIX)
    message(STATUS "Build system is POSIX.")
else ()
    message(STATUS "Build system is NOT POSIX.")
endif ()

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
# Propagate configured version to code as a string macro KTE_VERSION_STR
add_compile_definitions(KTE_VERSION_STR="v${KTE_VERSION}")

message(STATUS "Build system: ${CMAKE_HOST_SYSTEM_NAME}")

if (${BUILD_GUI})
    include(cmake/imgui.cmake)
endif ()

# NCurses for terminal mode
find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})

set(COMMON_SOURCES
        GapBuffer.cc
        PieceTable.cc
        Buffer.cc
        Editor.cc
        Command.cc
        KKeymap.cc
        TerminalInputHandler.cc
        TerminalRenderer.cc
        TerminalFrontend.cc
)

set(COMMON_HEADERS
        GapBuffer.h
        PieceTable.h
        Buffer.h
        Editor.h
        AppendBuffer.h
        Command.h
        KKeymap.h
        InputHandler.h
        TerminalInputHandler.h
        Renderer.h
        TerminalRenderer.h
        Frontend.h
        TerminalFrontend.h
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

target_link_libraries(kte ${CURSES_LIBRARIES})

# No GUI font-related preprocessor defines are emitted; GUI uses embedded font.

if (${BUILD_GUI})
    # Add GUI support to kte so it can be started with -g
    target_sources(kte PRIVATE
            Font.h
            GUIRenderer.cc
            GUIRenderer.h
            GUIInputHandler.cc
            GUIInputHandler.h
            GUIFrontend.cc
            GUIFrontend.h)
    target_compile_definitions(kte PRIVATE KTE_BUILD_GUI=1)
    target_link_libraries(kte imgui)

    # kge (GUI-first) executable
    add_executable(kge
            main.cc
            ${COMMON_SOURCES}
            ${COMMON_HEADERS}
            GUIRenderer.cc
            GUIRenderer.h
            GUIInputHandler.cc
            GUIInputHandler.h
            GUIFrontend.cc
            GUIFrontend.h)
    target_compile_definitions(kge PRIVATE KTE_BUILD_GUI=1 KTE_DEFAULT_GUI=1)
    target_link_libraries(kge ${CURSES_LIBRARIES} imgui)

    # No GUI font-related preprocessor defines are emitted; GUI uses embedded font.
endif ()
