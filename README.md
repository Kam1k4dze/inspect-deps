# inspect-deps

A command-line tool for analyzing ELF shared library dependencies. It walks the dependency graph of a binary and outputs the tree structure. On systems with `pacman`, it can optionally resolve libraries to their owning packages.

## Capabilities

- **Dependency Tree**: Visualizes the hierarchy of shared libraries.
- **Package Resolution**: Maps libraries to system packages (Arch Linux/libalpm only).
- **Minimal Set**: Computes the smallest set of packages required to run a binary.
- **Trace**: Explains why a specific library is present in the graph.
- **Export**: Outputs data to JSON or DOT for external processing.
- **Portable**: Built as a static executable (glibc dynamic), loading `libalpm` only if available.

## Limitations

- **Architecture**: Currently targets `x86_64` ELF binaries.
- **Package Manager**: Package resolution only works with `libalpm` (Pacman).
- **Loader Simulation**: RPATH/RUNPATH handling is simplified; it does not fully replicate `ld.so` behavior.
- **Dynamic Loading**: Only analyzes `DT_NEEDED` entries; libraries loaded via `dlopen` at runtime are not detected.

## Build

Requirements:
- CMake 3.25+
- C++23 compiler (GCC 13+ or Clang 16+)
- `pkg-config` (for finding libalpm)

```bash
cmake -S . -B build
cmake --build build --parallel
```

## Usage

```bash
# Show dependency tree
./build/inspect-deps /usr/bin/curl --tree

# List minimal package dependencies
./build/inspect-deps /usr/bin/vim --pkg-list

# Find why libssl is needed
./build/inspect-deps /usr/bin/curl --why libssl.so.3

# Export to JSON/DOT
./build/inspect-deps /usr/bin/git --json
./build/inspect-deps /usr/bin/git --dot > graph.dot

# Generate shell completions
./build/inspect-deps --completions fish > ~/.config/fish/completions/inspect-deps.fish
```

## Requirements (Runtime)

- Linux (x86_64)
- `pacman` (optional, for package resolution)


