#include "vm/vm.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace {

class RawTerminal final {
public:
    RawTerminal() {
        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            throw std::runtime_error("tcgetattr failed");
        }

        auto raw = original_;
        raw.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
        raw.c_oflag &= static_cast<tcflag_t>(~OPOST);
        raw.c_cflag |= CS8;
        raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            throw std::runtime_error("tcsetattr failed");
        }
    }

    ~RawTerminal() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    }

    RawTerminal(const RawTerminal&) = delete;
    RawTerminal& operator=(const RawTerminal&) = delete;

private:
    termios original_{};
};

void write_stdout(std::string_view text) {
    while (!text.empty()) {
        const auto count = write(STDOUT_FILENO, text.data(), text.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        text.remove_prefix(static_cast<std::size_t>(count));
    }
}

void resize_guest(whoami::vm::VM& machine) {
    winsize window_size{};
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &window_size) == 0) {
        machine.resize_terminal(window_size.ws_col, window_size.ws_row);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto project_root = std::filesystem::path{WHOAMI_SOURCE_DIR};
        whoami::vm::VMConfig config{
            .bios_image = project_root / "os/output/images/fw_jump.bin",
            .kernel_image = project_root / "os/output/images/Image",
            .rootfs_image = project_root / "os/output/images/rootfs.ext4",
        };

        if (argc == 4) {
            config.bios_image = argv[1];
            config.kernel_image = argv[2];
            config.rootfs_image = argv[3];
        } else if (argc != 1) {
            std::cerr << "usage: " << argv[0]
                      << " [fw_jump.bin Image rootfs.ext4]\n";
            return EXIT_FAILURE;
        }

        whoami::vm::VM machine{std::move(config)};
        machine.set_output_handler(write_stdout);
        machine.start();

        std::cout << "TinyEMU terminal sample. Press Ctrl-A then x to quit.\r\n";
        RawTerminal terminal;
        resize_guest(machine);

        bool escape_pending = false;
        while (machine.is_running()) {
            machine.tick();

            pollfd descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
            if (poll(&descriptor, 1, 1) <= 0 || !(descriptor.revents & POLLIN)) {
                continue;
            }

            std::array<char, 256> input{};
            const auto length = read(STDIN_FILENO, input.data(), input.size());
            if (length <= 0) {
                continue;
            }

            for (ssize_t index = 0; index < length; ++index) {
                const auto character = input[static_cast<std::size_t>(index)];
                if (escape_pending) {
                    escape_pending = false;
                    if (character == 'x') {
                        machine.stop();
                        break;
                    }
                    if (character == '\x01') {
                        machine.send_input("\x01");
                        continue;
                    }
                    machine.send_input("\x01");
                }

                if (character == '\x01') {
                    escape_pending = true;
                } else {
                    machine.send_input(std::string_view{&character, 1});
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "emulator sample: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
