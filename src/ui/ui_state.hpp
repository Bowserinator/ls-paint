#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "about-window.h"
#include "app/app_controller.hpp"
#include "appwindow.h"
#include "edit-colors-window.h"
#include "private/slint_timer.h"
#include "render/slint_bridge.hpp"

namespace lsp {

class UiState {
public:
  UiState(AppWindow &window, AppController &controller);

  // The Edit Colors window, made on first use. Its state lives in the window
  // itself, so the tests read it here rather than from a global.
  [[nodiscard]] lsp_colors::EditColorsWindow &color_window();
  // True while the Edit Colors window is on screen.
  [[nodiscard]] bool color_window_open() const noexcept;

private:
  // Pulls the Slint color pickers into the controller. Called before every
  // operation that needs them: only pointer presses synced them before, so
  // a cut or a rotate would erase with whatever color was picked last.
  void sync_colors();
  // Pulls the Shapes, Outline/Fill and thickness dropdowns into the tool
  // settings the shape tool reads when it starts a gesture.
  void sync_shape_style();
  // Pulls the font controls into the settings the text commit rasterizes.
  void sync_font_style();
  // Publishes the family list, narrowed to the names that contain `query`.
  void publish_font_families(std::string_view query);
  // Publishes the width of every line of the text the box holds, so the
  // overlay's underline and strikethrough rules stop at the end of their line.
  void publish_line_widths(std::string_view text);
  // Publishes the paper colour under the pointer for the status bar, blank
  // while the pointer is off the paper.
  void publish_hover_color();
  // True while a text box is being edited. The tool owns the flag, so the
  // editor's Slint side cannot get out of step with it.
  [[nodiscard]] bool text_open() const noexcept;
  // Closes the open text box, drawing its content into the paper. Safe to
  // call when no box is open: the editor's own commit, a click elsewhere and
  // a tool switch all land here.
  void do_commit_text(const std::string &text);
  void do_cancel_text();
  void refresh();

  // Writes one entry of the ribbon's palette.
  void set_palette_color(int index, const slint::Color &color);
  // The grid of custom colors the Edit Colors dialog edits. The ribbon's strip
  // seeds it, the dialog changes it, and OK moves one slot into the strip.
  void seed_dialog_customs();
  void write_dialog_custom(int index, const slint::Color &color);
  void rotate_strip(const slint::Color &color);

  // The Edit Colors picker. The hue, saturation and value it holds are the
  // colour being chosen, and every other field it shows follows from them.
  void ensure_color_window();
  // The window the picker's callbacks come from, or nothing when it has never
  // been made. Every one of those callbacks is only reached while it exists.
  [[nodiscard]] lsp_colors::EditColorsWindow *color_picker();
  void do_open_color_dialog();
  void do_color_accepted();
  void do_color_cancelled();
  void do_color_add_custom();
  void do_color_basic_picked(const slint::Color &color);
  void do_color_custom_picked(const slint::Color &color, int index);
  void do_color_ring_picked(float x, float y);
  void do_color_square_picked(float x, float y);
  void do_color_entered(const slint::SharedString &text);
  void do_color_hex_edited(const slint::SharedString &text);
  void push_color_dialog();

  // The About window, made on first use.
  void ensure_about_window();
  void do_open_about();

  // File actions, each of which may raise one of the modal surfaces first.
  enum class PendingAction { None, Exit, New, Open };

  void do_file_new();
  void do_file_open();
  void do_file_save();
  void do_file_save_as();
  void do_file_exit();
  void do_paste();
  void do_paste_from_file();

  // Work left half-done by a question the user has to answer.
  void ask_unsaved(PendingAction action);
  void run_pending();

  // The picker: which question it is asking, where it is looking, and the
  // listing it is showing.
  void show_file_dialog(EFileDialogMode mode);
  void refresh_file_dialog();
  void do_file_up();
  void do_file_navigate(int row);
  void do_file_filter_changed(int index);
  void do_file_ok();
  void do_file_cancel();

  // The canvas size dialog: the current size is offered, and only a size
  // inside the canvas limits is applied.
  void do_open_resize_dialog();
  void do_resize_ok();
  void do_resize_cancel();

  void show_message(const std::string &title, const std::string &text);
  // Pushes the file name and the unsaved marker into the window title.
  void set_title();
  // Puts the view back to a state that suits a document nobody has seen yet.
  void after_document_change();
  // Rebuilds the marching-ants outline and the grip positions from the
  // current selection. Driven by refresh() and by the animation timer.
  void update_ants();

  AppWindow &m_window;
  AppController &m_controller;
  SlintBridge m_bridge;
  // The two dialogs are windows of their own, made on first use and kept for
  // the life of the application.
  std::optional<slint::ComponentHandle<lsp_colors::EditColorsWindow>>
      m_color_window;
  std::optional<slint::ComponentHandle<lsp_about::AboutWindow>> m_about_window;
  // Kept so the colour picker can write into the palette's custom slots.
  std::shared_ptr<slint::VectorModel<slint::Color>> m_palette;
  // Every family the text tool can draw with, before the filter box narrows it.
  std::vector<std::string> m_font_families;
  // The Edit Colors dialog's own custom colors, shared with the dialog while it
  // is open.
  std::shared_ptr<slint::VectorModel<slint::Color>> m_dialog_customs;
  slint::Timer m_ants_timer;
  int m_ant_phase = 0;
  bool m_ants_visible = false;
  bool m_text_open = false;
  // Timed here because Slint's pointer events carry no click count.
  std::chrono::steady_clock::time_point m_last_press;
  Point2f m_last_press_position;
  bool m_have_last_press = false;
  // Watched in refresh() so a tool that is left mid-gesture can release it.
  ToolId m_last_tool = ToolId::Pencil;

  PendingAction m_pending = PendingAction::None;
  std::filesystem::path m_file_dir;
  std::vector<std::filesystem::path> m_listing;
};

} // namespace lsp
