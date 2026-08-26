#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace whoami::vm::protocol {

inline constexpr std::uint32_t magic = 0x314D5641U; // "AVM1" in little endian.
inline constexpr std::uint16_t version = 1;
inline constexpr std::uint32_t max_payload_size = 4U * 1024U * 1024U;

enum class Message : std::uint16_t {
    console_input = 1,
    resize = 2,
    stop = 3,
    state = 0x100,
    console_output = 0x101,
    error = 0x102,
};

enum class State : std::uint8_t {
    stopped,
    starting,
    running,
    faulted,
};

struct Header {
    std::uint32_t magic_value{magic};
    std::uint16_t protocol_version{version};
    Message message{};
    std::uint32_t payload_size{};
};

struct ResizePayload {
    std::uint16_t columns{};
    std::uint16_t rows{};
};

static_assert(sizeof(Header) == 12);
static_assert(sizeof(ResizePayload) == 4);
static_assert(std::is_trivially_copyable_v<Header>);

inline void append_frame(std::vector<std::byte>& destination, Message message,
                         std::span<const std::byte> payload = {}) {
    const Header header{
        .message = message,
        .payload_size = static_cast<std::uint32_t>(payload.size()),
    };
    const auto old_size = destination.size();
    destination.resize(old_size + sizeof(header) + payload.size());
    std::memcpy(destination.data() + old_size, &header, sizeof(header));
    if (!payload.empty()) {
        std::memcpy(destination.data() + old_size + sizeof(header),
                    payload.data(), payload.size());
    }
}

inline std::span<const std::byte> as_bytes(std::string_view value) {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

template <typename T>
    requires std::is_trivially_copyable_v<T>
inline std::span<const std::byte> as_bytes(const T& value) {
    return {reinterpret_cast<const std::byte*>(&value), sizeof(value)};
}

} // namespace whoami::vm::protocol
