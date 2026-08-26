#include "terminal/terminal.hpp"

#include <libtsm.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <utility>
#include <vector>

namespace whoami::terminal {

struct Terminal::Impl {
    tsm_screen* screen{};
    tsm_vte* vte{};
    InputHandler input_handler;
    std::vector<std::vector<std::string>> cells;
    std::vector<std::string> rendered_lines;
    std::uint16_t columns{};
    std::uint16_t rows{};

    static void write_to_guest(tsm_vte*, const char* bytes, std::size_t size, void* opaque) {
        auto& self = *static_cast<Impl*>(opaque);
        if (self.input_handler && size > 0) {
            self.input_handler(std::string_view(bytes, size));
        }
    }

    static int draw_cell(tsm_screen*, std::uint64_t, const std::uint32_t* characters,
                         std::size_t length, unsigned int width,
                         unsigned int x, unsigned int y,
                         const tsm_screen_attr*, tsm_age_t, void* opaque) {
        auto& self = *static_cast<Impl*>(opaque);
        if (x >= self.columns || y >= self.rows) {
            return 0;
        }
        std::string utf8;
        for (std::size_t index = 0; index < length; ++index) {
            std::array<char, 8> bytes{};
            const auto count = tsm_ucs4_to_utf8(characters[index], bytes.data());
            utf8.append(bytes.data(), count);
        }
        self.cells[y][x] = utf8.empty() ? " " : std::move(utf8);
        for (unsigned int offset = 1; offset < width && x + offset < self.columns; ++offset) {
            self.cells[y][x + offset].clear();
        }
        return 0;
    }

    void send(std::string_view bytes) const {
        if (input_handler && !bytes.empty()) {
            input_handler(bytes);
        }
    }
};

Terminal::Terminal(std::uint16_t columns, std::uint16_t rows)
    : impl_(std::make_unique<Impl>()) {
    if (tsm_screen_new(&impl_->screen, nullptr, nullptr) < 0) {
        throw std::runtime_error("cannot create libtsm screen");
    }
    if (tsm_vte_new(&impl_->vte, impl_->screen, &Impl::write_to_guest, impl_.get(),
                    nullptr, nullptr) < 0) {
        tsm_screen_unref(impl_->screen);
        impl_->screen = nullptr;
        throw std::runtime_error("cannot create libtsm VTE");
    }
    tsm_screen_set_max_sb(impl_->screen, 2'000);
    tsm_vte_set_palette(impl_->vte, "base16-dark");
    tsm_vte_set_backspace_sends_delete(impl_->vte, true);
    resize(columns, rows);
}

Terminal::~Terminal() {
    if (impl_->vte) tsm_vte_unref(impl_->vte);
    if (impl_->screen) tsm_screen_unref(impl_->screen);
}

void Terminal::set_input_handler(InputHandler handler) {
    impl_->input_handler = std::move(handler);
}

void Terminal::feed(std::string_view bytes) {
    if (!bytes.empty()) {
        tsm_vte_input(impl_->vte, bytes.data(), bytes.size());
    }
}

void Terminal::resize(std::uint16_t columns, std::uint16_t rows) {
    columns = std::max<std::uint16_t>(columns, 20);
    rows = std::max<std::uint16_t>(rows, 8);
    if (columns == impl_->columns && rows == impl_->rows) {
        return;
    }
    if (tsm_screen_resize(impl_->screen, columns, rows) < 0) {
        throw std::runtime_error("cannot resize libtsm screen");
    }
    impl_->columns = columns;
    impl_->rows = rows;
    impl_->cells.assign(rows, std::vector<std::string>(columns, " "));
    impl_->rendered_lines.assign(rows, std::string{});
}

void Terminal::text_input(std::string_view utf8) {
    impl_->send(utf8);
}

bool Terminal::key_down(SDL_Keycode key, SDL_Keymod modifiers) {
    const bool control = (modifiers & SDL_KMOD_CTRL) != 0;
    if (control && key >= SDLK_A && key <= SDLK_Z) {
        const char byte = static_cast<char>(key - SDLK_A + 1);
        impl_->send(std::string_view(&byte, 1));
        return true;
    }
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: impl_->send("\r"); return true;
    case SDLK_BACKSPACE: impl_->send("\x7f"); return true;
    case SDLK_TAB: impl_->send("\t"); return true;
    case SDLK_ESCAPE: impl_->send("\x1b"); return true;
    case SDLK_UP: impl_->send("\x1b[A"); return true;
    case SDLK_DOWN: impl_->send("\x1b[B"); return true;
    case SDLK_RIGHT: impl_->send("\x1b[C"); return true;
    case SDLK_LEFT: impl_->send("\x1b[D"); return true;
    case SDLK_HOME: impl_->send("\x1b[H"); return true;
    case SDLK_END: impl_->send("\x1b[F"); return true;
    case SDLK_PAGEUP: impl_->send("\x1b[5~"); return true;
    case SDLK_PAGEDOWN: impl_->send("\x1b[6~"); return true;
    case SDLK_INSERT: impl_->send("\x1b[2~"); return true;
    case SDLK_DELETE: impl_->send("\x1b[3~"); return true;
    default: return false;
    }
}

std::span<const std::string> Terminal::lines() {
    for (auto& row : impl_->cells) {
        std::fill(row.begin(), row.end(), " ");
    }
    tsm_screen_draw(impl_->screen, &Impl::draw_cell, impl_.get());
    for (std::size_t row = 0; row < impl_->cells.size(); ++row) {
        auto& line = impl_->rendered_lines[row];
        line.clear();
        for (const auto& cell : impl_->cells[row]) {
            line += cell;
        }
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
    }
    return impl_->rendered_lines;
}

std::uint16_t Terminal::columns() const noexcept { return impl_->columns; }
std::uint16_t Terminal::rows() const noexcept { return impl_->rows; }
std::uint16_t Terminal::cursor_column() const noexcept {
    return static_cast<std::uint16_t>(tsm_screen_get_cursor_x(impl_->screen));
}
std::uint16_t Terminal::cursor_row() const noexcept {
    return static_cast<std::uint16_t>(tsm_screen_get_cursor_y(impl_->screen));
}
bool Terminal::cursor_visible() const noexcept {
    return (tsm_screen_get_flags(impl_->screen) & TSM_SCREEN_HIDE_CURSOR) == 0;
}

} // namespace whoami::terminal
