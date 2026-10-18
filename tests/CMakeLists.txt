# Unit tests (no Slint) and end-to-end UI tests (real Slint window, synthetic
# input). The UI test lives in its own directory so that the Slint code
# generation writes its outputs here instead of next to the application's.

add_executable(lsp_tests selection_tests.cpp)
target_link_libraries(lsp_tests PRIVATE lsp_core)
target_include_directories(lsp_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lsp_tests COMMAND lsp_tests)

add_executable(lsp_raster_tests raster_test.cpp)
target_link_libraries(lsp_raster_tests PRIVATE lsp_core)
target_include_directories(lsp_raster_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lsp_raster_tests COMMAND lsp_raster_tests)

add_executable(lsp_color_tests color_tests.cpp)
target_link_libraries(lsp_color_tests PRIVATE lsp_core)
target_include_directories(lsp_color_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lsp_color_tests COMMAND lsp_color_tests)

add_executable(lsp_text_tests text_test.cpp)
target_link_libraries(lsp_text_tests PRIVATE lsp_core)
target_include_directories(lsp_text_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lsp_text_tests COMMAND lsp_text_tests)

add_executable(lsp_io_tests io_test.cpp)
target_link_libraries(lsp_io_tests PRIVATE lsp_core)
target_include_directories(lsp_io_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lsp_io_tests COMMAND lsp_io_tests)

add_executable(lsp_shape_tests shape_tool_test.cpp)
target_link_libraries(lsp_shape_tests PRIVATE lsp_core)
target_include_directories(lsp_shape_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lsp_shape_tests COMMAND lsp_shape_tests)

add_executable(lsp_store_tests store_test.cpp)
target_link_libraries(lsp_store_tests PRIVATE lsp_core)
target_include_directories(lsp_store_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME lsp_store_tests COMMAND lsp_store_tests)

add_executable(lsp_ui_tests
    ui_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/render/slint_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ui_state.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/cursor_overlay.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/file_picker.cpp
)
target_link_libraries(lsp_ui_tests PRIVATE lsp_core Slint::Slint)
target_include_directories(lsp_ui_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
# The window links the pickers in, so the test binary has to as well.
target_include_directories(lsp_ui_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/portable-file-dialogs)
slint_target_sources(lsp_ui_tests ${CMAKE_SOURCE_DIR}/ui/appwindow.slint)
# The dialog windows, in the same namespaces as the application's copies.
slint_target_sources(lsp_ui_tests ${CMAKE_SOURCE_DIR}/ui/dialogs/edit-colors-window.slint NAMESPACE lsp_colors)
slint_target_sources(lsp_ui_tests ${CMAKE_SOURCE_DIR}/ui/dialogs/about-window.slint NAMESPACE lsp_about)
slint_target_sources(lsp_ui_tests ${CMAKE_SOURCE_DIR}/ui/dialogs/confirm-dialog.slint NAMESPACE lsp_confirm)
slint_target_sources(lsp_ui_tests ${CMAKE_SOURCE_DIR}/ui/dialogs/message-dialog.slint NAMESPACE lsp_message)
slint_target_sources(lsp_ui_tests ${CMAKE_SOURCE_DIR}/ui/dialogs/resize-dialog.slint NAMESPACE lsp_resize)
add_test(NAME lsp_ui_tests COMMAND lsp_ui_tests)

# Screenshot tool: renders the app at a matrix of window sizes and writes one
# PNG per size, for checking ribbon layout by eye.
add_custom_target(snapshots
    COMMAND tests/snapshots/snapshot.sh $<TARGET_FILE:ls_paint> ${CMAKE_BINARY_DIR}/snapshots
    DEPENDS ls_paint
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Writing window screenshots to ${CMAKE_BINARY_DIR}/snapshots"
    VERBATIM
)

if(MSVC)
    foreach(target lsp_tests lsp_raster_tests lsp_color_tests lsp_text_tests lsp_io_tests lsp_shape_tests lsp_ui_tests)
        target_compile_options(${target} PRIVATE /W4)
    endforeach()
else()
    foreach(target lsp_tests lsp_raster_tests lsp_color_tests lsp_text_tests lsp_io_tests lsp_shape_tests lsp_ui_tests)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endforeach()
endif()

# The executables the parent scope's `check` target builds before it runs them.
set(LSP_TEST_TARGETS
    lsp_tests
    lsp_raster_tests
    lsp_color_tests
    lsp_text_tests
    lsp_io_tests
    lsp_shape_tests
    lsp_store_tests
    lsp_ui_tests
    PARENT_SCOPE
)
