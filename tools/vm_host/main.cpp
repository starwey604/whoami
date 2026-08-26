#include "vm/protocol.hpp"
#include "vm/vm.hpp"

#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using whoami::vm::protocol::Header;
using whoami::vm::protocol::Message;

struct Options {
    std::filesystem::path bios;
    std::filesystem::path kernel;
    std::filesystem::path rootfs;
    std::uint32_t memory{256};
};

struct Command {
    Message type{};
    std::vector<std::byte> payload;
};

struct CommandQueue {
    std::mutex mutex;
    std::deque<Command> commands;
    std::atomic_bool reader_finished{false};
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }
        const std::string_view value = argv[++index];
        if (argument == "--bios") options.bios = value;
        else if (argument == "--kernel") options.kernel = value;
        else if (argument == "--rootfs") options.rootfs = value;
        else if (argument == "--memory") {
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(),
                                                       options.memory);
            if (error != std::errc{} || end != value.data() + value.size()) {
                throw std::invalid_argument("invalid VM memory size");
            }
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }
    if (options.bios.empty() || options.kernel.empty() || options.rootfs.empty()) {
        throw std::invalid_argument("--bios, --kernel and --rootfs are required");
    }
    return options;
}

bool read_exact(char* destination, std::size_t size) {
    std::cin.read(destination, static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(std::cin.gcount()) == size;
}

void read_commands(CommandQueue& queue) {
    for (;;) {
        Header header{};
        if (!read_exact(reinterpret_cast<char*>(&header), sizeof(header))) break;
        if (header.magic_value != whoami::vm::protocol::magic ||
            header.protocol_version != whoami::vm::protocol::version ||
            header.payload_size > whoami::vm::protocol::max_payload_size) {
            break;
        }
        Command command{header.message, std::vector<std::byte>(header.payload_size)};
        if (header.payload_size > 0 &&
            !read_exact(reinterpret_cast<char*>(command.payload.data()), command.payload.size())) {
            break;
        }
        const bool stop = command.type == Message::stop;
        {
            std::lock_guard lock(queue.mutex);
            queue.commands.push_back(std::move(command));
        }
        if (stop) break;
    }
    queue.reader_finished = true;
}

void write_frame(Message message, std::span<const std::byte> payload = {}) {
    const Header header{.message = message,
                        .payload_size = static_cast<std::uint32_t>(payload.size())};
    std::cout.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!payload.empty()) {
        std::cout.write(reinterpret_cast<const char*>(payload.data()),
                        static_cast<std::streamsize>(payload.size()));
    }
    std::cout.flush();
}

void write_state(whoami::vm::protocol::State state) {
    write_frame(Message::state, whoami::vm::protocol::as_bytes(state));
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        write_state(whoami::vm::protocol::State::starting);

        whoami::vm::VM machine({
            .bios_image = options.bios,
            .kernel_image = options.kernel,
            .rootfs_image = options.rootfs,
            .memory_size_mebibytes = options.memory,
        });
        machine.set_output_handler([](std::string_view bytes) {
            write_frame(Message::console_output, whoami::vm::protocol::as_bytes(bytes));
        });
        machine.start();

        CommandQueue queue;
        std::thread reader([&queue] { read_commands(queue); });
        write_state(whoami::vm::protocol::State::running);

        bool running = true;
        while (running) {
            std::deque<Command> commands;
            {
                std::lock_guard lock(queue.mutex);
                commands.swap(queue.commands);
            }
            for (const auto& command : commands) {
                switch (command.type) {
                case Message::console_input:
                    machine.send_input(std::string_view{
                        reinterpret_cast<const char*>(command.payload.data()), command.payload.size()});
                    break;
                case Message::resize:
                    if (command.payload.size() == sizeof(whoami::vm::protocol::ResizePayload)) {
                        whoami::vm::protocol::ResizePayload resize{};
                        std::memcpy(&resize, command.payload.data(), sizeof(resize));
                        machine.resize_terminal(resize.columns, resize.rows);
                    }
                    break;
                case Message::stop:
                    running = false;
                    break;
                default:
                    break;
                }
            }
            if (running) {
                machine.tick(500'000);
            }
        }
        machine.stop();
        write_state(whoami::vm::protocol::State::stopped);
        reader.join();
        return 0;
    } catch (const std::exception& error) {
        const std::string_view message = error.what();
        write_frame(Message::error, whoami::vm::protocol::as_bytes(message));
        write_state(whoami::vm::protocol::State::faulted);
        std::cerr << "whoami-vm-host: " << error.what() << '\n';
        return 1;
    }
}
