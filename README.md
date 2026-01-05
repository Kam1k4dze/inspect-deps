# inspect-deps

A command-line tool to statically analyze ELF shared library dependencies and optionally map them to Arch Linux packages.
![dot](preview/dot.png)

## Features

- Dependency tree visualization (handles cycles).
- **Safer than ldd**: Performs static analysis (ELF parsing) without executing the binary, making it safe for inspecting untrusted files.
- RPATH/RUNPATH support (including $ORIGIN).
- Package resolution via libalpm (Pacman).
- Minimal package dependency calculation.
- Explanation of library presence.
- JSON and DOT export.
- Standalone operation; libalpm loaded dynamically.

## Limitations

- x86_64 architecture only.
- Package resolution requires libalpm.
- Does not detect dlopen-loaded libraries.

## Requirements

- Linux (x86_64)
- libalpm (optional, for package resolution)

## Build

Build requirements:

- CMake 3.28+
- C++23 compiler (GCC 13+ or Clang 16+)

```bash
cmake -S . -B build
cmake --build build --parallel
# binary at build/inspect-deps
```

## Installation

PKGBUILD included. Available on AUR: https://aur.archlinux.org/packages/inspect-deps. Package resolution requires
pacman/libalpm.

## Usage

```bash
inspect-deps [options] <binary> [mode]
```

### Global Options

These options apply to all output modes:

- `--no-pkg`: Disable package resolution.
- `--full-path`: Show full library paths instead of SONAMEs.
- `--show-stdlib`: Show standard library dependencies (glibc, etc.).
- `--no-header`: Suppress header (for default output).
- ANSI colors are used automatically when stdout is a TTY.

### Modes

#### Default (no mode flag)

Compact tabular summary of libraries.

Columns:

- Library: SONAME (or full path with `--full-path`).
- Package: Arch package (if libalpm available and enabled).
- Depth: Graph distance from root.
- Required By: Immediate parent (or "-" if none; "(+)" for multiple).

#### Dependency Tree (`--tree`)

Visualizes the dependency graph.

```bash
inspect-deps /usr/bin/curl --tree
```

![Dependency tree example](preview/tree.png)

> **Note**: Repeated nodes (diamonds) are marked with `(+)` and not expanded. Circular dependencies are marked with
`(cycle)`.

#### Package List (`--pkg-list`)

List minimal set of packages required by the binary.
**Note**: If the binary itself belongs to a package, that package is excluded from the list, showing only external dependencies.

```bash
inspect-deps /usr/bin/curl --pkg-list
> brotli krb5 libnghttp2 libnghttp3 libpsl libssh2 zstd
```

#### Explain (`--why <lib>`)

Explain why a library is needed (shows dependency chain).

```bash
inspect-deps /usr/bin/curl --why libcrypto.so.3
```

![Why example](preview/why.png)

#### JSON / DOT (`--json`, `--dot`)

Export dependency graph.

```bash
inspect-deps /usr/bin/git --json
inspect-deps /usr/bin/git --dot > graph.dot
```

#### Generate completions:

```bash
inspect-deps --completions fish > ~/.config/fish/completions/inspect-deps.fish
```
