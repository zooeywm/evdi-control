# evdi-control

`evdi-control` is a C++17 command-line utility for:

- creating and removing managed EVDI virtual displays,
- listing visible outputs,
- advertising requested modes to the system through EVDI EDID.

## Vendored `evdi_lib.h`

Arch Linux `evdi-dkms` ships `libevdi.so` but not the public development header.
This repository vendors the official upstream `library/evdi_lib.h` from DisplayLink so the
program can build without a separate `libevdi-dev` package.

Official upstream:

- `https://github.com/DisplayLink/evdi`

## Build dependencies

Ubuntu 20.04:

```bash
sudo apt install build-essential cmake
```

Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Usage

```bash
./build/evdi-control --help
sudo ./build/evdi-control add --count 10 --width 1920 --height 1080 --refresh 60
./build/evdi-control list
./build/evdi-control outputs
sudo ./build/evdi-control remove evdi1
sudo ./build/evdi-control remove --all
```

## Notes

- `evdi-control` does not call `xrandr`, Mutter DBus APIs, or compositor-specific tools.
  It only creates or removes EVDI displays and exposes modes via EDID.
- Whether a desktop session enables the new output or switches to a specific mode is outside
  the scope of this tool and is handled by the active display stack.
- State files are stored in `$XDG_RUNTIME_DIR/evdi-control` when available, otherwise
  `/tmp/evdi-control-<uid>`.
