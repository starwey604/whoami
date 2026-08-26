#include "vm/vm_client.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef WHOAMI_VM_HOST_PATH
#define WHOAMI_VM_HOST_PATH "whoami_vm_host"
#endif

#ifndef WHOAMI_SOURCE_DIR
#define WHOAMI_SOURCE_DIR "."
#endif

namespace whoami::vm {

struct VMClient::Impl {
    SDL_Process* process{};
    SDL_IOStream* input{};
    SDL_IOStream* output{};
    protocol::State state{protocol::State::stopped};
    OutputHandler output_handler;
    StateHandler state_handler;
    std::vector<std::byte> incoming;
    std::vector<std::byte> outgoing;
    std::size_t outgoing_offset{};
    std::string fault_message;

    void set_state(protocol::State new_state, std::string_view detail = {}) {
        state = new_state;
        if (state_handler) {
            state_handler(state, detail);
        }
    }

    void flush_output() {
        if (!input || outgoing_offset >= outgoing.size()) {
            return;
        }
        const auto remaining = outgoing.size() - outgoing_offset;
        const auto written = SDL_WriteIO(input, outgoing.data() + outgoing_offset, remaining);
        outgoing_offset += written;
        if (outgoing_offset == outgoing.size()) {
            outgoing.clear();
            outgoing_offset = 0;
        }
    }

    void parse_input() {
        for (;;) {
            if (incoming.size() < sizeof(protocol::Header)) {
                return;
            }
            protocol::Header header{};
            std::memcpy(&header, incoming.data(), sizeof(header));
            if (header.magic_value != protocol::magic ||
                header.protocol_version != protocol::version) {
                incoming.erase(incoming.begin());
                continue;
            }
            if (header.payload_size > protocol::max_payload_size) {
                incoming.erase(incoming.begin());
                continue;
            }
            const auto frame_size = sizeof(header) + header.payload_size;
            if (incoming.size() < frame_size) {
                return;
            }
            const auto* payload = incoming.data() + sizeof(header);
            switch (header.message) {
            case protocol::Message::console_output:
                if (output_handler && header.payload_size > 0) {
                    output_handler(std::string_view{
                        reinterpret_cast<const char*>(payload), header.payload_size});
                }
                break;
            case protocol::Message::state:
                if (header.payload_size == sizeof(protocol::State)) {
                    protocol::State new_state{};
                    std::memcpy(&new_state, payload, sizeof(new_state));
                    set_state(new_state);
                }
                break;
            case protocol::Message::error:
                fault_message.assign(reinterpret_cast<const char*>(payload), header.payload_size);
                set_state(protocol::State::faulted, fault_message);
                break;
            default:
                break;
            }
            incoming.erase(incoming.begin(), incoming.begin() + static_cast<std::ptrdiff_t>(frame_size));
        }
    }
};

VMClient::VMClient() : impl_(std::make_unique<Impl>()) {}
VMClient::~VMClient() { stop(); }

void VMClient::set_output_handler(OutputHandler handler) {
    impl_->output_handler = std::move(handler);
}

void VMClient::set_state_handler(StateHandler handler) {
    impl_->state_handler = std::move(handler);
}

void VMClient::start() {
    if (impl_->process) {
        throw std::logic_error("VM host is already running");
    }
    const std::filesystem::path images =
        std::filesystem::path(WHOAMI_SOURCE_DIR) / "os/output/images";
    const std::array required{
        images / "fw_jump.bin",
        images / "Image",
        images / "rootfs.ext4",
    };
    for (const auto& path : required) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("VM image is missing: " + path.string());
        }
    }

    const std::string host = WHOAMI_VM_HOST_PATH;
    const std::string bios = required[0].string();
    const std::string kernel = required[1].string();
    const std::string rootfs = required[2].string();
    const char* arguments[]{
        host.c_str(), "--bios", bios.c_str(), "--kernel", kernel.c_str(),
        "--rootfs", rootfs.c_str(), "--memory", "256", nullptr,
    };
    impl_->process = SDL_CreateProcess(arguments, true);
    if (!impl_->process) {
        throw std::runtime_error(std::string("cannot start VM host: ") + SDL_GetError());
    }
    impl_->input = SDL_GetProcessInput(impl_->process);
    impl_->output = SDL_GetProcessOutput(impl_->process);
    if (!impl_->input || !impl_->output) {
        stop();
        throw std::runtime_error(std::string("cannot open VM host pipes: ") + SDL_GetError());
    }
    impl_->set_state(protocol::State::starting);
}

void VMClient::update() {
    if (!impl_->process) {
        return;
    }
    impl_->flush_output();
    std::array<std::byte, 64 * 1024> buffer{};
    for (;;) {
        const auto count = SDL_ReadIO(impl_->output, buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        impl_->incoming.insert(impl_->incoming.end(), buffer.begin(), buffer.begin() + count);
        impl_->parse_input();
    }

    int exit_code{};
    if (SDL_WaitProcess(impl_->process, false, &exit_code)) {
        if (impl_->state != protocol::State::faulted && exit_code != 0) {
            impl_->fault_message = "VM host exited with code " + std::to_string(exit_code);
            impl_->set_state(protocol::State::faulted, impl_->fault_message);
        } else if (impl_->state != protocol::State::faulted) {
            impl_->set_state(protocol::State::stopped);
        }
        SDL_DestroyProcess(impl_->process);
        impl_->process = nullptr;
        impl_->input = nullptr;
        impl_->output = nullptr;
    }
}

void VMClient::send_input(std::string_view bytes) {
    if (!impl_->process || bytes.empty()) {
        return;
    }
    protocol::append_frame(impl_->outgoing, protocol::Message::console_input,
                           protocol::as_bytes(bytes));
    impl_->flush_output();
}

void VMClient::resize_terminal(std::uint16_t columns, std::uint16_t rows) {
    if (!impl_->process || columns == 0 || rows == 0) {
        return;
    }
    const protocol::ResizePayload resize{columns, rows};
    protocol::append_frame(impl_->outgoing, protocol::Message::resize,
                           protocol::as_bytes(resize));
    impl_->flush_output();
}

void VMClient::stop() noexcept {
    if (!impl_->process) {
        return;
    }
    try {
        protocol::append_frame(impl_->outgoing, protocol::Message::stop);
        impl_->flush_output();
        for (int attempt = 0; attempt < 50; ++attempt) {
            update();
            if (!impl_->process) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        SDL_KillProcess(impl_->process, true);
        int exit_code{};
        SDL_WaitProcess(impl_->process, true, &exit_code);
        SDL_DestroyProcess(impl_->process);
    } catch (...) {
        SDL_KillProcess(impl_->process, true);
        SDL_DestroyProcess(impl_->process);
    }
    impl_->process = nullptr;
    impl_->input = nullptr;
    impl_->output = nullptr;
    impl_->state = protocol::State::stopped;
}

protocol::State VMClient::state() const noexcept { return impl_->state; }

} // namespace whoami::vm
