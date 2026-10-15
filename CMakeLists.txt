cmake_minimum_required(VERSION 3.21)
project(ls_paint LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(SLINT_CARGO_RELEASE_PROFILE ON CACHE BOOL "Force Slint Cargo release profile" FORCE)

FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG        v1.17.1
    SOURCE_SUBDIR  api/cpp
)

set(SLINT_FEATURE_INTERPRETER OFF CACHE BOOL "" FORCE)
set(SLINT_FEATURE_LIVE_PREVIEW OFF CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_FEMTOVG OFF CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_SOFTWARE ON CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_SKIA OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Slint)

# stb: single-header image and font decoding. Only headers are used, so this
# just populates the source tree.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# clip: the system clipboard, used for cut, copy and paste. Vendored because its
# CMake probes the platform libraries it links against.
set(CLIP_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIP_TESTS OFF CACHE BOOL "" FORCE)
set(CLIP_INSTALL OFF CACHE BOOL "" FORCE)
set(CLIP_ENABLE_IMAGE ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/clip)

# Everything that does not touch Slint, so tests can link it without the GUI.
add_library(lsp_core STATIC
    src/app/app_controller.cpp
    src/app/input_router.cpp
    src/brushes/brush_engine.cpp
    src/core/blend_modes.cpp
    src/core/canvas_document.cpp
    src/core/compositor.cpp
    src/core/pixel_buffer.cpp
    src/core/raster.cpp
    src/history/history_stack.cpp
    src/history/commands/paint_stroke_command.cpp
    src/history/commands/selection_edit_command.cpp
    src/io/clipboard.cpp
    src/io/document_io.cpp
    src/io/stb_impl.cpp
    src/selection/selection_mask.cpp
    src/selection/selection_ops.cpp
    src/tools/brush_stroke_tool.cpp
    src/tools/dropper_tool.cpp
    src/tools/fill_tool.cpp
    src/tools/selection_tool.cpp
    src/tools/shape_tool.cpp
    src/tools/stroke_recorder.cpp
    src/tools/text_tool.cpp
    src/tools/tool_registry.cpp
    src/ui/shortcuts.cpp
)
target_include_directories(lsp_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lsp_core PRIVATE clip)
# The text tool rasterizes glyphs with stb_truetype and reads the bundled
# DejaVu faces; the image formats come from the same headers. SYSTEM keeps the
# third-party code out of our warnings.
target_include_directories(lsp_core SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_definitions(lsp_core PRIVATE LSP_FONT_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts")

add_executable(ls_paint
    src/main.cpp
    src/render/slint_bridge.cpp
    src/ui/ui_state.cpp
)
target_include_directories(ls_paint PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ls_paint PRIVATE lsp_core Slint::Slint)
slint_target_sources(ls_paint ui/appwindow.slint)

if(MSVC)
    foreach(target lsp_core ls_paint)
        target_compile_options(${target} PRIVATE /W4)
    endforeach()
else()
    foreach(target lsp_core ls_paint)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endforeach()
endif()

if (WIN32)
    add_custom_command(TARGET ls_paint POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:ls_paint> $<TARGET_FILE_DIR:ls_paint> COMMAND_EXPAND_LISTS)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
