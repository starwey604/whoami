#include "game/request_feed.hpp"

#include <algorithm>
#include <array>

namespace whoami::game {
namespace {

struct ScheduledRequest {
    std::string_view id;
    std::string_view title;
    std::string_view objective;
    std::string_view priority;
    float reveal_at;
};

constexpr std::array schedule{
    ScheduledRequest{"REQUEST 01", "检查系统环境并报告", "确认内核、架构与 Python", "常规", 1.2F},
    ScheduledRequest{"REQUEST 02", "定位异常日志", "查看启动日志中的告警", "优先", 8.5F},
    ScheduledRequest{"REQUEST 03", "读取工作说明", "找到并阅读 /root/SKILL.md", "常规", 16.0F},
};

float ease_out_cubic(float value) {
    const float inverse = 1.0F - std::clamp(value, 0.0F, 1.0F);
    return 1.0F - inverse * inverse * inverse;
}

} // namespace

RequestFeed::RequestFeed(bool reduced_motion) : reduced_motion_(reduced_motion) {}

void RequestFeed::update(float delta_seconds) {
    elapsed_seconds_ += std::clamp(delta_seconds, 0.0F, 0.1F);
}

std::array<VisibleRequest, 3> RequestFeed::visible_requests(
    std::size_t& count) const noexcept {
    std::array<VisibleRequest, 3> visible{};
    count = 0;
    for (const auto& request : schedule) {
        if (elapsed_seconds_ < request.reveal_at) {
            continue;
        }
        const float linear = reduced_motion_
                                 ? 1.0F
                                 : (elapsed_seconds_ - request.reveal_at) / 0.24F;
        visible[count++] = {
            request.id,
            request.title,
            request.objective,
            request.priority,
            ease_out_cubic(linear),
        };
    }
    return visible;
}

float RequestFeed::elapsed_seconds() const noexcept { return elapsed_seconds_; }

} // namespace whoami::game
