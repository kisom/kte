cmake_minimum_required(VERSION 3.15)
project(kte)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(KTE_VERSION "1.2.0")

# Default to terminal-only build to avoid SDL/OpenGL dependency by default.
# Enable with -DBUILD_GUI=ON when SDL2/OpenGL/Freetype are available.
set(BUILD_GUI ON CACHE BOOL "Enable building the graphical version.")
set(BUILD_TESTS OFF CACHE BOOL "Enable building test programs.")
option(KTE_USE_PIECE_TABLE "Use PieceTable instead of GapBuffer implementation" ON)
set(KTE_FONT_SIZE "18.0" CACHE STRING "Default font size for GUI")
option(KTE_UNDO_DEBUG "Enable undo instrumentation logs" OFF)
option(KTE_ENABLE_TREESITTER "Enable optional Tree-sitter highlighter adapter" OFF)

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

# Detect availability of get_wch (wide-char input) in the curses headers
include(CheckSymbolExists)
set(CMAKE_REQUIRED_INCLUDES ${CURSES_INCLUDE_DIR})
check_symbol_exists(get_wch "ncurses.h" KTE_HAVE_GET_WCH_IN_NCURSES)
if (NOT KTE_HAVE_GET_WCH_IN_NCURSES)
	# Some systems expose curses headers as <curses.h>
	check_symbol_exists(get_wch "curses.h" KTE_HAVE_GET_WCH_IN_CURSES)
endif ()
if (KTE_HAVE_GET_WCH_IN_NCURSES OR KTE_HAVE_GET_WCH_IN_CURSES)
	add_compile_definitions(KTE_HAVE_GET_WCH)
endif ()

set(SYNTAX_SOURCES
	syntax/HighlighterEngine.cc
	syntax/CppHighlighter.cc
	syntax/HighlighterRegistry.cc
	syntax/NullHighlighter.cc
	syntax/JsonHighlighter.cc
	syntax/MarkdownHighlighter.cc
	syntax/ShellHighlighter.cc
	syntax/GoHighlighter.cc
	syntax/PythonHighlighter.cc
	syntax/RustHighlighter.cc
	syntax/LispHighlighter.cc
	syntax/SqlHighlighter.cc
	syntax/ErlangHighlighter.cc
	syntax/ForthHighlighter.cc
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
	lsp/UtfCodec.cc
	lsp/BufferChangeTracker.cc
	lsp/JsonRpcTransport.cc
	lsp/LspProcessClient.cc
	lsp/DiagnosticStore.cc
	lsp/TerminalDiagnosticDisplay.cc
	lsp/LspManager.cc

	${SYNTAX_SOURCES}
)

if (KTE_ENABLE_TREESITTER)
	list(APPEND SYNTAX_SOURCES
		syntax/TreeSitterHighlighter.cc)
endif ()

set(THEME_HEADERS
	themes/EInk.h
	themes/Gruvbox.h
	themes/Nord.h
	themes/Plan9.h
	themes/Solarized.h
	themes/ThemeHelpers.h
)

set(SYNTAX_HEADERS
	syntax/LanguageHighlighter.h
	syntax/HighlighterEngine.h
	syntax/CppHighlighter.h
	syntax/HighlighterRegistry.h
	syntax/NullHighlighter.h
	syntax/JsonHighlighter.h
	syntax/MarkdownHighlighter.h
	syntax/ShellHighlighter.h
	syntax/GoHighlighter.h
	syntax/PythonHighlighter.h
	syntax/RustHighlighter.h
	syntax/LispHighlighter.h
)

if (KTE_ENABLE_TREESITTER)
	list(APPEND SYNTAX_HEADERS
		syntax/TreeSitterHighlighter.h)
endif ()

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
	lsp/UtfCodec.h
	lsp/LspTypes.h
	lsp/BufferChangeTracker.h
	lsp/JsonRpcTransport.h
	lsp/LspClient.h
	lsp/LspProcessClient.h
	lsp/Diagnostic.h
	lsp/DiagnosticStore.h
	lsp/DiagnosticDisplay.h
	lsp/TerminalDiagnosticDisplay.h
	lsp/LspManager.h
	lsp/LspServerConfig.h
	ext/json.h
	ext/json_fwd.h

	${THEME_HEADERS}
	${SYNTAX_HEADERS}
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
# Ensure vendored headers (e.g., ext/json.h) are on the include path
target_include_directories(kte PRIVATE ${CMAKE_SOURCE_DIR}/ext)

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
	# Ensure vendored headers (e.g., ext/json.h) are on the include path for tests as well
	target_include_directories(test_undo PRIVATE ${CMAKE_SOURCE_DIR}/ext)
	if (KTE_ENABLE_TREESITTER)
		if (TREESITTER_INCLUDE_DIR)
			target_include_directories(test_undo PRIVATE ${TREESITTER_INCLUDE_DIR})
		endif ()
		if (TREESITTER_LIBRARY)
			target_link_libraries(test_undo ${TREESITTER_LIBRARY})
		endif ()
	endif ()

	# test_utfcodec executable for UTF conversion helpers
	add_executable(test_utfcodec
		test_utfcodec.cc
		${COMMON_SOURCES}
		${COMMON_HEADERS}
	)

	if (KTE_USE_PIECE_TABLE)
		target_compile_definitions(test_utfcodec PRIVATE KTE_USE_PIECE_TABLE=1)
	endif ()

	if (KTE_UNDO_DEBUG)
		target_compile_definitions(test_utfcodec PRIVATE KTE_UNDO_DEBUG=1)
	endif ()


	target_link_libraries(test_utfcodec ${CURSES_LIBRARIES})
	# Ensure vendored headers (e.g., ext/json.h) are on the include path for tests as well
	target_include_directories(test_utfcodec PRIVATE ${CMAKE_SOURCE_DIR}/ext)
	if (KTE_ENABLE_TREESITTER)
		if (TREESITTER_INCLUDE_DIR)
			target_include_directories(test_utfcodec PRIVATE ${TREESITTER_INCLUDE_DIR})
		endif ()
		if (TREESITTER_LIBRARY)
			target_link_libraries(test_utfcodec ${TREESITTER_LIBRARY})
		endif ()
	endif ()

	# test_transport executable for JSON-RPC framing
	add_executable(test_transport
		test_transport.cc
		${COMMON_SOURCES}
		${COMMON_HEADERS}
	)

	if (KTE_USE_PIECE_TABLE)
		target_compile_definitions(test_transport PRIVATE KTE_USE_PIECE_TABLE=1)
	endif ()

	if (KTE_UNDO_DEBUG)
		target_compile_definitions(test_transport PRIVATE KTE_UNDO_DEBUG=1)
	endif ()


	target_link_libraries(test_transport ${CURSES_LIBRARIES})
	# Ensure vendored headers (e.g., ext/json.h) are on the include path for tests as well
	target_include_directories(test_transport PRIVATE ${CMAKE_SOURCE_DIR}/ext)
	if (KTE_ENABLE_TREESITTER)
		if (TREESITTER_INCLUDE_DIR)
			target_include_directories(test_transport PRIVATE ${TREESITTER_INCLUDE_DIR})
		endif ()
		if (TREESITTER_LIBRARY)
			target_link_libraries(test_transport ${TREESITTER_LIBRARY})
		endif ()
	endif ()

	# test_lsp_decode executable for dispatcher decoding
	add_executable(test_lsp_decode
		test_lsp_decode.cc
		${COMMON_SOURCES}
		${COMMON_HEADERS}
	)

	if (KTE_USE_PIECE_TABLE)
		target_compile_definitions(test_lsp_decode PRIVATE KTE_USE_PIECE_TABLE=1)
	endif ()

	if (KTE_UNDO_DEBUG)
		target_compile_definitions(test_lsp_decode PRIVATE KTE_UNDO_DEBUG=1)
	endif ()


	target_link_libraries(test_lsp_decode ${CURSES_LIBRARIES})
	# Ensure vendored headers (e.g., ext/json.h) are on the include path for tests as well
	target_include_directories(test_lsp_decode PRIVATE ${CMAKE_SOURCE_DIR}/ext)
	if (KTE_ENABLE_TREESITTER)
		if (TREESITTER_INCLUDE_DIR)
			target_include_directories(test_lsp_decode PRIVATE ${TREESITTER_INCLUDE_DIR})
		endif ()
		if (TREESITTER_LIBRARY)
			target_link_libraries(test_lsp_decode ${TREESITTER_LIBRARY})
		endif ()
	endif ()
endif ()

if (${BUILD_GUI})
	target_sources(kte PRIVATE
		Font.h
		GUIConfig.cc
		GUIConfig.h
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
		GUIConfig.cc
		GUIConfig.h
		GUIRenderer.cc
		GUIRenderer.h
		GUIInputHandler.cc
		GUIInputHandler.h
		GUIFrontend.cc
		GUIFrontend.h)
	target_compile_definitions(kge PRIVATE KTE_BUILD_GUI=1 KTE_DEFAULT_GUI=1 KTE_FONT_SIZE=${KTE_FONT_SIZE})
	if (KTE_UNDO_DEBUG)
		target_compile_definitions(kge PRIVATE KTE_UNDO_DEBUG=1)
	endif ()
	target_link_libraries(kge ${CURSES_LIBRARIES} imgui)
	target_include_directories(kge PRIVATE ${CMAKE_SOURCE_DIR}/ext)

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
