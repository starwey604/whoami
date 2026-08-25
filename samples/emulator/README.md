# TinyEMU terminal sample

This target is built by the top-level CMake project and boots the Buildroot
RISC-V guest through `whoami::vm::VM`. It uses the host terminal only as a
temporary frontend; the game can use the same VM class with an SDL3 terminal
widget instead.

Build the guest first. Buildroot produces OpenSBI's `fw_jump.bin`, the Linux
`Image`, and an ext4 root filesystem exposed to Linux as `/dev/vda`.

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
os/output/images/rootfs.ext4
```

Alternatively, pass all three paths explicitly:

```sh
./build/samples/emulator/whoami_emulator_sample \
    path/to/fw_jump.bin path/to/Image path/to/rootfs.ext4
```

Input and output flow through the emulated VirtIO console (`console=hvc0`).
The root filesystem is copied into memory when the VM starts, so guest writes
are discarded when the sample exits. On a typical desktop the interpreter-only
boot takes roughly 20-30 seconds.
Press `Ctrl-A`, then `x` to exit the sample. `Ctrl-A`, then `Ctrl-A` sends a
literal `Ctrl-A` to Linux.
