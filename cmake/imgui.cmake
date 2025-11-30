find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Freetype)
if (DEFINED FREETYPE_INCLUDE_DIRS)
    add_definitions(-DIMGUI_ENABLE_FREETYPE)
    set(FREETYPE_SOURCES
            ext/imgui/misc/freetype/imgui_freetype.cpp
            ext/imgui/misc/freetype/imgui_freetype.h)
endif()

add_library(imgui STATIC
        # Main Imgui files
        ext/imgui/imgui.cpp
        ext/imgui/imgui.h
        ext/imgui/imgui_draw.cpp
        ext/imgui/imgui_tables.cpp
        ext/imgui/imgui_widgets.cpp
        ext/imgui/imgui_demo.cpp

        ext/imgui/backends/imgui_impl_sdl2.cpp
        ext/imgui/backends/imgui_impl_sdl2.h
        ext/imgui/backends/imgui_impl_opengl3.cpp
        ext/imgui/backends/imgui_impl_opengl3.h

        $<IF:$<TARGET_EXISTS:Freetype::Freetype>,${FREETYPE_SOURCES},>)
add_library(imgui::imgui ALIAS imgui)
target_link_libraries(imgui
        PUBLIC
        OpenGL::GL
        $<IF:$<TARGET_EXISTS:Freetype::Freetype>,Freetype::Freetype,>
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
target_include_directories(imgui PUBLIC
        ext/imgui/
        ext/imgui/backends/
        ext/imgui/misc/freetype
        $<IF:$<TARGET_EXISTS:Freetype::Freetype>,${FREETYPE_INCLUDE_DIRS},>)

include_directories(ext/ ${SDL2_INCLUDE_DIRS})
