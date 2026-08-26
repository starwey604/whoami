#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace whoami::game {

struct VisibleRequest {
    std::string_view id;
    std::string_view title;
    std::string_view objective;
    std::string_view priority;
    float arrival_progress{};
};

/// Deterministic request sequence for the first playable slice.
/// The eventual narrative system can replace this producer without changing UI.
class RequestFeed final {
public:
    explicit RequestFeed(bool reduced_motion = false);

    void update(float delta_seconds);
    [[nodiscard]] std::array<VisibleRequest, 3> visible_requests(
        std::size_t& count) const noexcept;
    [[nodiscard]] float elapsed_seconds() const noexcept;

private:
    float elapsed_seconds_{};
    bool reduced_motion_{};
};

} // namespace whoami::game
