#include "render/gpu_renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <iostream>

namespace {

using whoami::render::Color;
using whoami::render::GpuRenderer;
using whoami::render::Rect;

void draw_mvp_shell(GpuRenderer& renderer) {
    constexpr Color panel{0.018F, 0.075F, 0.105F, 0.96F};
    constexpr Color panel_alt{0.025F, 0.105F, 0.142F, 0.92F};
    constexpr Color border{0.12F, 0.53F, 0.68F, 0.58F};
    constexpr Color focus{0.25F, 0.84F, 0.96F, 1.0F};
    constexpr Color text{0.91F, 0.97F, 0.98F, 1.0F};
    constexpr Color muted{0.51F, 0.70F, 0.75F, 1.0F};
    constexpr Color accent{0.23F, 0.72F, 0.85F, 1.0F};

    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const float scale = std::clamp(std::min(w / 1920.0F, h / 1080.0F), 0.72F, 1.6F);
    const float safe = 52.0F * scale;
    const float gap = 18.0F * scale;
    const float header_h = 68.0F * scale;
    const float footer_h = 54.0F * scale;
    const float content_y = safe + header_h + gap;
    const float content_h = h - content_y - footer_h - safe - gap;
    const float left_w = std::clamp(w * 0.22F, 270.0F * scale, 420.0F * scale);
    const float right_w = std::clamp(w * 0.18F, 230.0F * scale, 340.0F * scale);
    const Rect header{safe, safe, w - safe * 2.0F, header_h};
    const Rect requests{safe, content_y, left_w, content_h};
    const Rect tools{w - safe - right_w, content_y, right_w, content_h};
    const Rect terminal{requests.x + requests.w + gap, content_y,
                        tools.x - gap - (requests.x + requests.w + gap), content_h};

    renderer.fill_rect(header, panel_alt);
    renderer.stroke_rect(header, std::max(1.0F, scale), border);
    renderer.text("WHOAMI // AGENT WORKSTATION", header.x + 22.0F * scale,
                  header.y + 43.0F * scale, 24.0F * scale, text);
    renderer.text("●  VM STARTING", header.x + header.w - 196.0F * scale,
                  header.y + 42.0F * scale, 18.0F * scale, accent, true);

    renderer.fill_rect(requests, panel);
    renderer.stroke_rect(requests, std::max(1.0F, scale), border);
    renderer.text("INCOMING REQUESTS", requests.x + 20.0F * scale,
                  requests.y + 38.0F * scale, 19.0F * scale, muted);
    const Rect card{requests.x + 16.0F * scale, requests.y + 68.0F * scale,
                    requests.w - 32.0F * scale, 126.0F * scale};
    renderer.fill_rect(card, panel_alt);
    renderer.stroke_rect(card, 2.0F * scale, accent);
    renderer.text("REQUEST 01", card.x + 16.0F * scale, card.y + 31.0F * scale,
                  16.0F * scale, accent, true);
    renderer.text("检查系统环境并报告", card.x + 16.0F * scale,
                  card.y + 66.0F * scale, 20.0F * scale, text);
    renderer.text("优先级：常规", card.x + 16.0F * scale,
                  card.y + 99.0F * scale, 16.0F * scale, muted);

    renderer.fill_rect(terminal, {0.006F, 0.031F, 0.045F, 0.99F});
    renderer.stroke_rect(terminal, 3.0F * scale, focus);
    renderer.fill_rect({terminal.x, terminal.y, terminal.w, 46.0F * scale}, panel_alt);
    renderer.text("TERMINAL /dev/hvc0", terminal.x + 18.0F * scale,
                  terminal.y + 31.0F * scale, 18.0F * scale, text, true);
    renderer.text("Booting Linux virtual machine...", terminal.x + 22.0F * scale,
                  terminal.y + 82.0F * scale, 18.0F * scale, muted, true);
    renderer.text("█", terminal.x + 22.0F * scale,
                  terminal.y + 116.0F * scale, 18.0F * scale, focus, true);

    renderer.fill_rect(tools, panel);
    renderer.stroke_rect(tools, std::max(1.0F, scale), border);
    renderer.text("AGENT TOOLS", tools.x + 20.0F * scale,
                  tools.y + 38.0F * scale, 19.0F * scale, muted);
    constexpr const char* tool_names[] = {"[1] READ SKILL", "[2] NOTES", "[3] SUBMIT"};
    for (int i = 0; i < 3; ++i) {
        const Rect button{tools.x + 16.0F * scale,
                          tools.y + (68.0F + i * 62.0F) * scale,
                          tools.w - 32.0F * scale, 48.0F * scale};
        renderer.fill_rect(button, panel_alt);
        renderer.stroke_rect(button, std::max(1.0F, scale), border);
        renderer.text(tool_names[i], button.x + 14.0F * scale,
                      button.y + 31.0F * scale, 17.0F * scale, text, true);
    }

    renderer.text("ESC 退出    F1 帮助    TAB 切换焦点", safe,
                  h - safe - 12.0F * scale, 17.0F * scale, muted);
}

} // namespace

int main() {
    try {
        GpuRenderer renderer(1440, 900, "WHOAMI — AI Agent Workstation");
        bool running = true;
        while (running) {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                    running = false;
                }
            }
            renderer.begin_frame();
            draw_mvp_shell(renderer);
            renderer.present({0.004F, 0.019F, 0.031F, 1.0F});
        }
    } catch (const std::exception& error) {
        std::cerr << "whoami: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
