cmake_minimum_required(VERSION 3.15)
project(kte)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(KTE_VERSION "0.9.0")

# Default to terminal-only build to avoid SDL/OpenGL dependency by default.
# Enable with -DBUILD_GUI=ON when SDL2/OpenGL/Freetype are available.
set(BUILD_GUI OFF CACHE BOOL "Enable building the graphical version.")
set(BUILD_TESTS OFF CACHE BOOL "Enable building test programs.")
option(KTE_USE_PIECE_TABLE "Use PieceTable instead of GapBuffer implementation" ON)
set(KTE_FONT_SIZE "18.0" CACHE STRING "Default font size for GUI")

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
        TestInputHandler.cc
        TestRenderer.cc
        TestFrontend.cc
        UndoNode.cc
        UndoTree.cc
        UndoSystem.cc
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
        TestInputHandler.h
        TestRenderer.h
        TestFrontend.h
        UndoNode.h
        UndoTree.h
        UndoSystem.h
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


    target_link_libraries(test_undo ${CURSES_LIBRARIES})
endif ()

if (${BUILD_GUI})
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
    target_compile_definitions(kge PRIVATE KTE_BUILD_GUI=1 KTE_DEFAULT_GUI=1 KTE_FONT_SIZE=${KTE_FONT_SIZE})
    target_link_libraries(kge ${CURSES_LIBRARIES} imgui)

    # On macOS, build kge as a proper .app bundle
    if (APPLE)
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
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/kge-Info.plist")

        install(TARGETS kge
                BUNDLE DESTINATION .
        )
    else ()
        install(TARGETS kge
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif ()
    # Install kge man page only when GUI is built
    install(FILES docs/kge.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif ()
