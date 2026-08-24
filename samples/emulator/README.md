# TinyEMU terminal sample

This target is built by the top-level CMake project and boots the Buildroot
RISC-V guest through `whoami::vm::VM`. It uses the host terminal only as a
temporary frontend; the game can use the same VM class with an SDL3 terminal
widget instead.

Build the guest first. The configured Buildroot image embeds its root filesystem
into `Image` and produces OpenSBI's `fw_jump.bin`.

```sh
./os/build.sh
cmake -S . -B build -G Ninja
cmake --build build --target whoami_emulator_sample
./build/samples/emulator/whoami_emulator_sample
```

The sample reads these default images:

```text
os/output/images/fw_jump.bin
os/output/images/Image
```

Alternatively, pass both paths explicitly:

```sh
./build/samples/emulator/whoami_emulator_sample path/to/fw_jump.bin path/to/Image
```

Input and output flow through the emulated VirtIO console (`console=hvc0`).
Press `Ctrl-A`, then `x` to exit the sample. `Ctrl-A`, then `Ctrl-A` sends a
literal `Ctrl-A` to Linux.
