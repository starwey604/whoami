#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace whoami::render {

struct Color {
    float r{};
    float g{};
    float b{};
    float a{1.0F};
};

struct Rect {
    float x{};
    float y{};
    float w{};
    float h{};
};

/// Small immediate-mode 2D renderer backed exclusively by SDL_GPU.
///
/// UI code records rectangles and UTF-8 text every frame. The renderer batches
/// them into streaming vertex/index buffers and submits one SDL_GPU render pass.
class GpuRenderer final {
public:
    GpuRenderer(int width, int height, std::string_view title);
    ~GpuRenderer();

    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;

    [[nodiscard]] SDL_Window* window() const noexcept;
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    void begin_frame();
    void fill_rect(Rect rect, Color color);
    void stroke_rect(Rect rect, float thickness, Color color);
    void text(std::string_view value, float x, float y, float point_size,
              Color color, bool monospace = false);
    void present(Color clear_color);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace whoami::render
