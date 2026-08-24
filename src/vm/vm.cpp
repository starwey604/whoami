#include "vm/vm.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

extern "C" {
#include "cutils.h"
#include "virtio.h"
#include "machine.h"
}

namespace whoami::vm {
namespace {

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("cannot open VM image: " + path.string());
    }

    const auto size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("VM image is empty: " + path.string());
    }
    if (static_cast<std::uintmax_t>(size) > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("VM image is too large: " + path.string());
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
        throw std::runtime_error("cannot read VM image: " + path.string());
    }
    return data;
}

} // namespace

struct VM::Impl {
    explicit Impl(VMConfig new_config) : config(std::move(new_config)) {}

    static void write_console(void* opaque, const std::uint8_t* data, int length) {
        auto& self = *static_cast<Impl*>(opaque);
        if (length > 0 && self.output_handler) {
            self.output_handler(std::string_view{
                reinterpret_cast<const char*>(data), static_cast<std::size_t>(length)});
        }
    }

    static int read_console(void*, std::uint8_t*, int) {
        // Input is pulled explicitly by tick() once the guest posts a buffer.
        return 0;
    }

    VMConfig config;
    OutputHandler output_handler;
    std::vector<std::uint8_t> bios;
    std::vector<std::uint8_t> kernel;
    std::deque<std::uint8_t> pending_input;
    CharacterDevice console{};
    VirtMachine* machine{nullptr};
};

VM::VM(VMConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

VM::~VM() {
    stop();
}

void VM::set_output_handler(OutputHandler handler) {
    impl_->output_handler = std::move(handler);
}

void VM::start() {
    if (is_running()) {
        throw std::logic_error("VM is already running");
    }
    if (impl_->config.memory_size_mebibytes == 0) {
        throw std::invalid_argument("VM memory_size_mebibytes must be positive");
    }

    impl_->bios = read_binary_file(impl_->config.bios_image);
    impl_->kernel = read_binary_file(impl_->config.kernel_image);

    constexpr std::uint64_t kMebibyte = 1024 * 1024;
    constexpr std::uint64_t kKernelAlignment = 2 * 1024 * 1024;
    const auto memory_size = static_cast<std::uint64_t>(impl_->config.memory_size_mebibytes) * kMebibyte;
    if (kKernelAlignment + impl_->kernel.size() > memory_size) {
        throw std::invalid_argument("VM memory is too small for the kernel image");
    }

    impl_->console.opaque = impl_.get();
    impl_->console.write_data = &Impl::write_console;
    impl_->console.read_data = &Impl::read_console;

    VirtMachineParams parameters{};
    parameters.vmc = &riscv_machine_class;
    parameters.machine_name = const_cast<char*>("riscv64");
    parameters.ram_size = memory_size;
    parameters.rtc_real_time = TRUE;
    parameters.console = &impl_->console;
    parameters.cmdline = const_cast<char*>(impl_->config.kernel_command_line.c_str());
    parameters.files[VM_FILE_BIOS].buf = impl_->bios.data();
    parameters.files[VM_FILE_BIOS].len = static_cast<int>(impl_->bios.size());
    parameters.files[VM_FILE_KERNEL].buf = impl_->kernel.data();
    parameters.files[VM_FILE_KERNEL].len = static_cast<int>(impl_->kernel.size());

    impl_->machine = virt_machine_init(&parameters);
    if (!impl_->machine) {
        throw std::runtime_error("TinyEMU could not initialize the RISC-V VM");
    }
}

void VM::stop() noexcept {
    if (impl_->machine) {
        virt_machine_end(impl_->machine);
        impl_->machine = nullptr;
    }
    impl_->pending_input.clear();
}

bool VM::is_running() const noexcept {
    return impl_->machine != nullptr;
}

void VM::tick(std::uint32_t max_cycles) {
    if (!is_running()) {
        throw std::logic_error("VM is not running");
    }

    if (!impl_->pending_input.empty() && virtio_console_can_write_data(impl_->machine->console_dev)) {
        const auto capacity = virtio_console_get_write_len(impl_->machine->console_dev);
        const auto count = static_cast<int>(std::min<std::size_t>(
            impl_->pending_input.size(), static_cast<std::size_t>(std::max(capacity, 0))));
        if (count > 0) {
            std::vector<std::uint8_t> bytes(impl_->pending_input.begin(),
                                            impl_->pending_input.begin() + count);
            const auto written = virtio_console_write_data(impl_->machine->console_dev,
                                                            bytes.data(), count);
            for (int index = 0; index < written; ++index) {
                impl_->pending_input.pop_front();
            }
        }
    }

    virt_machine_interp(impl_->machine, static_cast<int>(std::min<std::uint32_t>(
        max_cycles, static_cast<std::uint32_t>(std::numeric_limits<int>::max()))));
}

void VM::send_input(std::string_view bytes) {
    if (!is_running()) {
        throw std::logic_error("VM is not running");
    }
    for (const auto byte : bytes) {
        impl_->pending_input.push_back(static_cast<std::uint8_t>(byte));
    }
}

void VM::resize_terminal(std::uint16_t columns, std::uint16_t rows) {
    if (!is_running()) {
        throw std::logic_error("VM is not running");
    }
    if (columns > 0 && rows > 0 && impl_->machine->console_dev) {
        virtio_console_resize_event(impl_->machine->console_dev, columns, rows);
    }
}

} // namespace whoami::vm
