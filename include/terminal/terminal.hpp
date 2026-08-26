#pragma once

#include <SDL3/SDL_keycode.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace whoami::terminal {

/// libtsm-backed VT terminal model. It owns no window or GPU resources.
class Terminal final {
public:
    using InputHandler = std::function<void(std::string_view)>;

    Terminal(std::uint16_t columns = 80, std::uint16_t rows = 30);
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    void set_input_handler(InputHandler handler);
    void feed(std::string_view bytes);
    void resize(std::uint16_t columns, std::uint16_t rows);

    void text_input(std::string_view utf8);
    bool key_down(SDL_Keycode key, SDL_Keymod modifiers);

    [[nodiscard]] std::span<const std::string> lines();
    [[nodiscard]] std::uint16_t columns() const noexcept;
    [[nodiscard]] std::uint16_t rows() const noexcept;
    [[nodiscard]] std::uint16_t cursor_column() const noexcept;
    [[nodiscard]] std::uint16_t cursor_row() const noexcept;
    [[nodiscard]] bool cursor_visible() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace whoami::terminal
