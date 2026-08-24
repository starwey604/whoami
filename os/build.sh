#!/usr/bin/env bash
# Build the RISC-V guest image used by the game.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
buildroot_dir="$script_dir/buildroot"
defconfig="$script_dir/configs/os_defconfig"
output_dir="${OUTPUT_DIR:-$script_dir/output}"
jobs="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"

usage() {
    cat <<'EOF'
Usage: ./os/build.sh [build|menuconfig|savedefconfig|clean|distclean|<make-target>...]

Builds the RISC-V Linux guest from configs/os_defconfig into os/output/.

Environment:
  JOBS=N             Parallel build jobs (default: available CPU count)
  OUTPUT_DIR=PATH    Buildroot output directory (default: os/output)

Examples:
  ./os/build.sh
  JOBS=8 ./os/build.sh
  ./os/build.sh menuconfig
  ./os/build.sh savedefconfig
EOF
}

if [[ ! -f "$buildroot_dir/Makefile" || ! -f "$defconfig" ]]; then
    printf 'error: expected Buildroot and defconfig below %s\n' "$script_dir" >&2
    exit 1
fi

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

mkdir -p "$output_dir"
output_dir="$(cd -- "$output_dir" && pwd)"

configure() {
    make -C "$buildroot_dir" O="$output_dir" BR2_DEFCONFIG="$defconfig" defconfig
}

case "${1:-build}" in
    build)
        shift || true
        configure
        exec make -C "$buildroot_dir" O="$output_dir" -j "$jobs" "$@"
        ;;
    menuconfig)
        configure
        exec make -C "$buildroot_dir" O="$output_dir" menuconfig
        ;;
    savedefconfig)
        if [[ ! -f "$output_dir/.config" ]]; then
            configure
        fi
        exec make -C "$buildroot_dir" O="$output_dir" BR2_DEFCONFIG="$defconfig" savedefconfig
        ;;
    clean|distclean)
        exec make -C "$buildroot_dir" O="$output_dir" "$1"
        ;;
    *)
        configure
        exec make -C "$buildroot_dir" O="$output_dir" -j "$jobs" "$@"
        ;;
esac
