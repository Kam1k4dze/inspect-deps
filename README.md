# inspect-deps

A lightweight ELF dependency analyzer for Arch Linux. It visualizes the dependency tree of a binary and resolves shared libraries to their owning Pacman packages.

## Features

- **Tree View**: Visualize dependencies as a tree.
- **Package Resolution**: Automatically maps libraries to Arch Linux packages.
- **Minimal Set**: Calculates the minimal set of packages required to run the binary.
- **Why**: Trace why a specific library is included in the dependency graph.
- **JSON/DOT Output**: Export data for external tools.

## Usage

```bash
# Show dependency tree
inspect-deps /usr/bin/curl --tree

# List minimal package dependencies
inspect-deps /usr/bin/vim --pkg-list

# Find why libssl is needed
inspect-deps /usr/bin/curl --why libssl.so.3

# Export to JSON/DOT
inspect-deps /usr/bin/git --json
inspect-deps /usr/bin/git --dot
```

## Requirements

- Python 3.11+
- `readelf` (binutils)
- `pacman` (Arch Linux)

