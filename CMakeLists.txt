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

# Compile @image-url assets into the binary instead of reading them from disk
# at run time. embed-for-software-renderer pre-decodes them for the renderer
# this build enables.
set(DEFAULT_SLINT_EMBED_RESOURCES embed-files CACHE STRING "How the Slint compiler embeds @image-url assets" FORCE)

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

# sqlite3: the vendored public-domain amalgamation, for the settings store. It
# is a library of its own so the 250k-line file is compiled once and nothing
# else pays to rebuild it.
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party/sqlite3)

# Everything that does not touch Slint, so tests can link it without the GUI.
add_library(lsp_core STATIC
    src/app/app_controller.cpp
    src/app/command_line.cpp
    src/app/input_router.cpp
    src/brushes/brush.cpp
    src/brushes/brush_engine.cpp
    src/core/blend_modes.cpp
    src/core/blur.cpp
    src/core/warp.cpp
    src/core/canvas_document.cpp
    src/core/color.cpp
    src/core/compositor.cpp
    src/core/pixel_buffer.cpp
    src/core/pixel_delta.cpp
    src/core/raster.cpp
    src/history/history_stack.cpp
    src/history/commands/paint_stroke_command.cpp
    src/history/commands/selection_edit_command.cpp
    src/io/clipboard.cpp
    src/io/document_io.cpp
    src/io/stb_impl.cpp
    src/selection/selection_mask.cpp
    src/selection/selection_ops.cpp
    src/store/store_sqlite.cpp
    src/text/font_catalog.cpp
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
target_link_libraries(lsp_core PRIVATE clip sqlite3)
# The text tool rasterizes glyphs with stb_truetype and reads the bundled
# DejaVu faces; the image formats come from the same headers. SYSTEM keeps the
# third-party code out of our warnings.
target_include_directories(lsp_core SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_definitions(lsp_core PRIVATE LSP_FONT_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts")

# The installed fonts come from the platform: fontconfig on Linux and DirectWrite
# on Windows.
if (WIN32)
    target_sources(lsp_core PRIVATE src/text/font_catalog_windows.cpp)
    target_link_libraries(lsp_core PRIVATE dwrite shell32)
else()
    find_package(Fontconfig REQUIRED)
    target_sources(lsp_core PRIVATE src/text/font_catalog_linux.cpp)
    target_link_libraries(lsp_core PRIVATE Fontconfig::Fontconfig)
endif()

add_executable(ls_paint
    src/main.cpp
    src/render/slint_bridge.cpp
    src/ui/cursor_overlay.cpp
    src/ui/file_picker.cpp
    src/ui/ui_state.cpp
)
target_include_directories(ls_paint PRIVATE ${CMAKE_SOURCE_DIR}/src)
# portable-file-dialogs: the system's own open and save dialogs, header only.
target_include_directories(ls_paint SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/portable-file-dialogs)
find_package(Threads REQUIRED)
target_link_libraries(ls_paint PRIVATE lsp_core Slint::Slint Threads::Threads)
slint_target_sources(ls_paint ui/appwindow.slint)
# The two dialogs are real windows, shown from the application code. Each is
# compiled in its own namespace: the files they share with the ribbon would
# otherwise put the same symbols in the generated code twice.
slint_target_sources(ls_paint ui/dialogs/edit-colors-window.slint NAMESPACE lsp_colors)
slint_target_sources(ls_paint ui/dialogs/about-window.slint NAMESPACE lsp_about)
slint_target_sources(ls_paint ui/dialogs/confirm-dialog.slint NAMESPACE lsp_confirm)
slint_target_sources(ls_paint ui/dialogs/message-dialog.slint NAMESPACE lsp_message)
slint_target_sources(ls_paint ui/dialogs/resize-dialog.slint NAMESPACE lsp_resize)

if(WIN32)
    target_sources(ls_paint PRIVATE icons/ls_paint.rc)
    # The resource names the icon from the project root, which the resource
    # compiler only finds on the include path.
    target_include_directories(ls_paint PRIVATE ${CMAKE_SOURCE_DIR})
    # The window is the whole user interface, so the program must start without
    # a console window beside it.
    set_target_properties(ls_paint PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        # A Windows subsystem program starts at WinMain; this one has a main.
        target_link_options(ls_paint PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    elseif(NOT MSVC)
        target_link_options(ls_paint PRIVATE -mwindows)
    endif()
endif()

if(MSVC)
    foreach(target lsp_core ls_paint)
        # Source and string literals are UTF-8, which the path conversions assume.
        target_compile_options(${target} PRIVATE /W4 /utf-8)
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
    # The tests build on their own, so the default build produces only the
    # application; `check` builds the test executables and runs them.
    add_subdirectory(tests EXCLUDE_FROM_ALL)
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${LSP_TEST_TARGETS}
        USES_TERMINAL
    )
endif()

include(GNUInstallDirs)

# A desktop entry points at the binary and its icon by name, so all three are
# installed together. The installed binary finds Slint and clip through the
# library directory beside it, which the build-tree rpath does not cover.
set_target_properties(ls_paint PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS ls_paint RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES $<TARGET_FILE:clip> DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ls_paint.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES icons/ls_paint.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
