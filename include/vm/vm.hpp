#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace whoami::vm {

/// Files and boot arguments required by a RISC-V guest.
struct VMConfig {
    /// OpenSBI's fw_jump.bin image.
    std::filesystem::path bios_image;
    /// Buildroot's uncompressed RISC-V Image, with its initramfs embedded.
    std::filesystem::path kernel_image;
    std::uint32_t memory_size_mebibytes{256};
    std::string kernel_command_line{"console=hvc0"};
};

/// A frame-driven virtual machine suitable for the game's terminal widget.
///
/// Call tick() from the game loop and forward terminal keystrokes with
/// send_input(). Output is delivered synchronously through set_output_handler(),
/// so the SDL layer can append it to its terminal buffer without TinyEMU knowing
/// anything about rendering.
class VM final {
public:
    using OutputHandler = std::function<void(std::string_view)>;

    explicit VM(VMConfig config);
    ~VM();

    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;
    VM(VM&&) = delete;
    VM& operator=(VM&&) = delete;

    void set_output_handler(OutputHandler handler);

    /// Loads the firmware and kernel, then creates the emulated RISC-V machine.
    void start();
    void stop() noexcept;

    [[nodiscard]] bool is_running() const noexcept;

    /// Executes at most max_cycles of guest work and forwards pending input.
    void tick(std::uint32_t max_cycles = 500'000);

    /// Queues UTF-8 or terminal-control bytes for the guest VirtIO console.
    void send_input(std::string_view bytes);

    /// Notifies the guest of the terminal dimensions, in character cells.
    void resize_terminal(std::uint16_t columns, std::uint16_t rows);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace whoami::vm
