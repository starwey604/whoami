#pragma once

#include "vm/protocol.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace whoami::vm {

/// Cross-platform game-side connection to the isolated TinyEMU host process.
/// update() is non-blocking and is intended to run once per SDL frame.
class VMClient final {
public:
    using OutputHandler = std::function<void(std::string_view)>;
    using StateHandler = std::function<void(protocol::State, std::string_view)>;

    VMClient();
    ~VMClient();

    VMClient(const VMClient&) = delete;
    VMClient& operator=(const VMClient&) = delete;

    void set_output_handler(OutputHandler handler);
    void set_state_handler(StateHandler handler);

    void start();
    void update();
    void send_input(std::string_view bytes);
    void resize_terminal(std::uint16_t columns, std::uint16_t rows);
    void stop() noexcept;

    [[nodiscard]] protocol::State state() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace whoami::vm
