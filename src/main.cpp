#include "game/request_feed.hpp"
#include "render/gpu_renderer.hpp"
#include "terminal/terminal.hpp"
#include "vm/vm_client.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace {

using whoami::render::Color;
using whoami::render::GpuRenderer;
using whoami::render::Rect;
using whoami::terminal::Terminal;

struct Layout {
    float scale{};
    float safe{};
    float gap{};
    Rect header;
    Rect requests;
    Rect terminal;
    Rect tools;
};

Layout calculate_layout(const GpuRenderer& renderer) {
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
    const Rect requests{safe, content_y, left_w, content_h};
    const Rect tools{w - safe - right_w, content_y, right_w, content_h};
    return {
        .scale = scale,
        .safe = safe,
        .gap = gap,
        .header = {safe, safe, w - safe * 2.0F, header_h},
        .requests = requests,
        .terminal = {requests.x + requests.w + gap, content_y,
                     tools.x - gap - (requests.x + requests.w + gap), content_h},
        .tools = tools,
    };
}

std::string_view state_label(whoami::vm::protocol::State state) {
    using enum whoami::vm::protocol::State;
    switch (state) {
    case starting: return "●  VM STARTING";
    case running: return "●  VM ONLINE";
    case faulted: return "!  VM FAULT";
    case stopped: return "○  VM STOPPED";
    }
    return "?  VM UNKNOWN";
}

void draw_mvp_shell(GpuRenderer& renderer, Terminal& terminal_model,
                    whoami::vm::protocol::State vm_state,
                    std::string_view vm_detail,
                    const whoami::game::RequestFeed& request_feed) {
    constexpr Color panel{0.018F, 0.075F, 0.105F, 0.96F};
    constexpr Color panel_alt{0.025F, 0.105F, 0.142F, 0.92F};
    constexpr Color border{0.12F, 0.53F, 0.68F, 0.58F};
    constexpr Color focus{0.25F, 0.84F, 0.96F, 1.0F};
    constexpr Color text{0.91F, 0.97F, 0.98F, 1.0F};
    constexpr Color muted{0.51F, 0.70F, 0.75F, 1.0F};
    constexpr Color accent{0.23F, 0.72F, 0.85F, 1.0F};

    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const auto layout = calculate_layout(renderer);
    const float scale = layout.scale;
    const auto header = layout.header;
    const auto requests = layout.requests;
    const auto tools = layout.tools;
    const auto terminal = layout.terminal;

    renderer.fill_rect(header, panel_alt);
    renderer.stroke_rect(header, std::max(1.0F, scale), border);
    renderer.text("WHOAMI // AGENT WORKSTATION", header.x + 22.0F * scale,
                  header.y + 43.0F * scale, 24.0F * scale, text);
    const auto status_color = vm_state == whoami::vm::protocol::State::faulted
                                  ? Color{0.98F, 0.45F, 0.40F, 1.0F} : accent;
    renderer.text(state_label(vm_state), header.x + header.w - 196.0F * scale,
                  header.y + 42.0F * scale, 18.0F * scale, status_color, true);

    renderer.fill_rect(requests, panel);
    renderer.stroke_rect(requests, std::max(1.0F, scale), border);
    renderer.text("INCOMING REQUESTS", requests.x + 20.0F * scale,
                  requests.y + 38.0F * scale, 19.0F * scale, muted);
    std::size_t request_count{};
    const auto visible_requests = request_feed.visible_requests(request_count);
    if (request_count == 0) {
        renderer.text("等待用户连接…", requests.x + 20.0F * scale,
                      requests.y + 88.0F * scale, 17.0F * scale, muted);
    }
    for (std::size_t index = 0; index < request_count; ++index) {
        const auto& request = visible_requests[index];
        const float progress = request.arrival_progress;
        const float offset_x = (1.0F - progress) * -34.0F * scale;
        const float card_height = 118.0F * scale;
        const Rect card{requests.x + 16.0F * scale + offset_x,
                        requests.y + (68.0F + index * 130.0F) * scale,
                        requests.w - 32.0F * scale, card_height};
        renderer.fill_rect(card, {panel_alt.r, panel_alt.g, panel_alt.b,
                                  panel_alt.a * progress});
        renderer.stroke_rect(card, (index + 1 == request_count ? 2.0F : 1.0F) * scale,
                             {accent.r, accent.g, accent.b, progress});
        renderer.text(request.id, card.x + 14.0F * scale, card.y + 27.0F * scale,
                      15.0F * scale, {accent.r, accent.g, accent.b, progress}, true);
        renderer.text(request.title, card.x + 14.0F * scale, card.y + 57.0F * scale,
                      18.0F * scale, {text.r, text.g, text.b, progress});
        renderer.text(request.objective, card.x + 14.0F * scale, card.y + 82.0F * scale,
                      14.0F * scale, {muted.r, muted.g, muted.b, progress});
        const std::string priority = "优先级：" + std::string(request.priority);
        renderer.text(priority, card.x + 14.0F * scale, card.y + 104.0F * scale,
                      14.0F * scale, {muted.r, muted.g, muted.b, progress});
    }

    renderer.fill_rect(terminal, {0.006F, 0.031F, 0.045F, 0.99F});
    renderer.stroke_rect(terminal, 3.0F * scale, focus);
    renderer.fill_rect({terminal.x, terminal.y, terminal.w, 46.0F * scale}, panel_alt);
    renderer.text("TERMINAL /dev/hvc0", terminal.x + 18.0F * scale,
                  terminal.y + 31.0F * scale, 18.0F * scale, text, true);
    const float font_size = 17.0F * scale;
    const float cell_width = 10.25F * scale;
    const float line_height = 22.0F * scale;
    const float text_x = terminal.x + 18.0F * scale;
    const float text_y = terminal.y + 74.0F * scale;
    const auto columns = static_cast<std::uint16_t>(std::max(
        20.0F, (terminal.w - 36.0F * scale) / cell_width));
    const auto rows = static_cast<std::uint16_t>(std::max(
        8.0F, (terminal.h - 88.0F * scale) / line_height));
    terminal_model.resize(columns, rows);

    const auto lines = terminal_model.lines();
    if (terminal_model.cursor_visible()) {
        renderer.fill_rect({
            text_x + terminal_model.cursor_column() * cell_width,
            text_y + terminal_model.cursor_row() * line_height - font_size,
            cell_width,
            line_height,
        }, {0.20F, 0.63F, 0.72F, 0.55F});
    }
    for (std::size_t row = 0; row < lines.size(); ++row) {
        if (!lines[row].empty()) {
            renderer.text(lines[row], text_x, text_y + row * line_height,
                          font_size, text, true);
        }
    }
    if (vm_state == whoami::vm::protocol::State::faulted && !vm_detail.empty()) {
        renderer.fill_rect({terminal.x + 12.0F * scale,
                            terminal.y + terminal.h - 56.0F * scale,
                            terminal.w - 24.0F * scale, 40.0F * scale},
                           {0.25F, 0.035F, 0.045F, 0.94F});
        renderer.text(vm_detail, terminal.x + 22.0F * scale,
                      terminal.y + terminal.h - 29.0F * scale,
                      16.0F * scale, {1.0F, 0.70F, 0.68F, 1.0F}, true);
    }

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

    renderer.text("ESC 退出    输入直接发送到 VM    F1 帮助", layout.safe,
                  h - layout.safe - 12.0F * scale, 17.0F * scale, muted);
}

} // namespace

int main() {
    try {
        GpuRenderer renderer(1440, 900, "WHOAMI — AI Agent Workstation");
        Terminal terminal_model;
        whoami::vm::VMClient vm;
        whoami::game::RequestFeed request_feed(
            std::getenv("WHOAMI_REDUCED_MOTION") != nullptr);
        std::string vm_detail;
        vm.set_output_handler([&terminal_model](std::string_view bytes) {
            terminal_model.feed(bytes);
        });
        vm.set_state_handler([&vm_detail](whoami::vm::protocol::State,
                                          std::string_view detail) {
            vm_detail = detail;
        });
        terminal_model.set_input_handler([&vm](std::string_view bytes) {
            vm.send_input(bytes);
        });
        vm.start();
        SDL_StartTextInput(renderer.window());

        std::uint16_t sent_columns{};
        std::uint16_t sent_rows{};
        auto previous_tick = SDL_GetTicksNS();
        bool running = true;
        while (running) {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                    running = false;
                } else if (event.type == SDL_EVENT_TEXT_INPUT) {
                    terminal_model.text_input(event.text.text);
                } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    terminal_model.key_down(event.key.key, event.key.mod);
                }
            }
            const auto current_tick = SDL_GetTicksNS();
            request_feed.update(static_cast<float>(current_tick - previous_tick) / 1'000'000'000.0F);
            previous_tick = current_tick;
            vm.update();
            renderer.begin_frame();
            draw_mvp_shell(renderer, terminal_model, vm.state(), vm_detail, request_feed);
            if (terminal_model.columns() != sent_columns || terminal_model.rows() != sent_rows) {
                sent_columns = terminal_model.columns();
                sent_rows = terminal_model.rows();
                vm.resize_terminal(sent_columns, sent_rows);
            }
            renderer.present({0.004F, 0.019F, 0.031F, 1.0F});
        }
        SDL_StopTextInput(renderer.window());
    } catch (const std::exception& error) {
        std::cerr << "whoami: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
