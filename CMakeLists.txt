cmake_minimum_required(VERSION 3.15)
project(kte)

set(CMAKE_CXX_STANDARD 17)

set(BUILD_GUI ON CACHE BOOL "Disable building the graphical version.")
option(KTE_USE_PIECE_TABLE "Use PieceTable instead of GapBuffer implementation" OFF)


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
            "-static"
            "$<$<CONFIG:DEBUG>:-g>"
            "$<$<CONFIG:RELEASE>:-O2>")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options("-stdlib=libc++")
    else ()
        # nothing special for gcc at the moment
    endif ()
endif ()
add_compile_definitions(KGE_PLATFORM=${CMAKE_HOST_SYSTEM_NAME})
add_compile_definitions(KGE_VERSION=${PROJECT_VERSION})

message(STATUS "Build system: ${CMAKE_HOST_SYSTEM_NAME}")

if (${BUILD_GUI})
    include(cmake/imgui.cmake)
endif ()

set(SOURCES
        GapBuffer.cpp
        PieceTable.cpp
        Buffer.cpp
        Editor.cpp
)

set(HEADERS
        GapBuffer.h
        PieceTable.h
        Buffer.h
        Editor.h
        AppendBuffer.h
)

add_executable(kte
        main.cpp
        ${SOURCES}
        ${HEADERS}
)

if (KTE_USE_PIECE_TABLE)
    target_compile_definitions(kte PRIVATE KTE_USE_PIECE_TABLE=1)
endif ()

if (${BUILD_GUI})
    target_link_libraries(kge imgui)
endif ()
