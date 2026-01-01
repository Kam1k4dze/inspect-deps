#!/usr/bin/env python3

import argparse
import json
import shutil
import subprocess
import sys
from collections import defaultdict, deque
from pathlib import Path
from typing import Dict, List, Optional, Tuple

NOISE_PREFIX = ("linux-vdso", "ld-linux")
GLIBC_PREFIX = ("libc.so", "libm.so", "libpthread.so", "librt.so", "libdl.so")


class SystemResolver:
    def __init__(self):
        self._lib_cache: Dict[str, str] = {}
        self._pkg_cache: Dict[str, str] = {}
        self._cache_populated = False
        self.has_pacman = shutil.which("pacman") is not None

    def _populate_ldconfig(self):
        if self._cache_populated: return
        try:
            out = subprocess.check_output(["ldconfig", "-p"], text=True, stderr=subprocess.DEVNULL)
            for line in out.splitlines():
                if "=>" not in line: continue
                parts = line.strip().split("=>")
                lib_entry = parts[0].strip().split(" ")[0]
                path = parts[1].strip()
                if lib_entry not in self._lib_cache: self._lib_cache[lib_entry] = path
        except:
            pass
        self._cache_populated = True

    def resolve_soname(self, soname: str) -> Optional[str]:
        self._populate_ldconfig()
        return self._lib_cache.get(soname)

    def batch_resolve_packages(self, paths: List[str]):
        if not self.has_pacman or not paths: return
        unknowns = [p for p in paths if p not in self._pkg_cache]
        for i in range(0, len(unknowns), 50):
            self._resolve_chunk(unknowns[i:i + 50])

    def _resolve_chunk(self, paths: List[str]):
        try:
            proc = subprocess.run(["pacman", "-Qo"] + paths, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True, check=False)
            for line in proc.stdout.splitlines():
                if " is owned by " in line:
                    parts = line.split(" is owned by ", 1)
                    path, pkg = parts[0].strip(), parts[1].strip().split(" ")[0]
                    self._pkg_cache[path] = pkg
        except:
            pass

    def get_package(self, path: str) -> str:
        return self._pkg_cache.get(path, "-")


SYS = SystemResolver()


def get_elf_metadata(path: str) -> Tuple[List[str], Optional[str]]:
    needed, rpath = [], None
    try:
        out = subprocess.check_output(["readelf", "-d", path], text=True, stderr=subprocess.DEVNULL)
        for line in out.splitlines():
            if "(NEEDED)" in line:
                needed.append(line.split("[", 1)[1].split("]", 1)[0])
            elif "(RPATH)" in line or "(RUNPATH)" in line:
                rpath = line.split("[", 1)[1].split("]", 1)[0]
    except Exception:
        pass
    return needed, rpath


class DepGraph:
    def __init__(self):
        self.forward = defaultdict(set)
        self.reverse = defaultdict(set)
        self.paths, self.depth, self.rpath = {}, {}, {}

    def build(self, root_path: str, show_stdlib: bool):
        root_name = Path(root_path).name
        self.paths[root_name], self.depth[root_name] = str(root_path), 0
        q = deque([root_name])
        while q:
            cur = q.popleft()
            needed, rpath = get_elf_metadata(self.paths[cur])
            if rpath: self.rpath[cur] = rpath
            for lib in needed:
                if lib.startswith(NOISE_PREFIX) or (not show_stdlib and lib.startswith(GLIBC_PREFIX)): continue
                self.forward[cur].add(lib)
                self.reverse[lib].add(cur)
                if lib not in self.paths:
                    res = SYS.resolve_soname(lib)
                    if res:
                        self.paths[lib], self.depth[lib] = res, self.depth[cur] + 1
                        q.append(lib)
        SYS.batch_resolve_packages(list(self.paths.values()))


def get_minimal_pkgs(graph: DepGraph, root: str) -> list[str]:
    lib_to_pkg = {l: SYS.get_package(p) for l, p in graph.paths.items() if SYS.get_package(p) != "-"}
    pkg_deps = defaultdict(set)
    for parent, kids in graph.forward.items():
        p_pkg = lib_to_pkg.get(parent, "__ROOT__" if parent == root else None)
        if not p_pkg: continue
        for k in kids:
            c_pkg = lib_to_pkg.get(k)
            if c_pkg and c_pkg != p_pkg: pkg_deps[p_pkg].add(c_pkg)

    transitive = set()
    for p, kids in pkg_deps.items():
        if p != "__ROOT__": transitive.update(kids)

    minimal = pkg_deps.get("__ROOT__", set()) - transitive
    return sorted(list(minimal)) if minimal or not pkg_deps.get("__ROOT__") else sorted(list(pkg_deps["__ROOT__"]))


def print_tree(graph: DepGraph, root: str):
    seen = set()

    def rec(n, pref="", last=True):
        pkg = SYS.get_package(graph.paths.get(n, ""))
        print(f"{pref}{'└── ' if last else '├── '}{n} \033[90m[{pkg}]\033[0m")
        if n in seen: return
        seen.add(n)
        kids = sorted(graph.forward.get(n, []))
        for i, k in enumerate(kids): rec(k, pref + ("    " if last else "│   "), i == len(kids) - 1)

    print(f"{root} \033[90m[{SYS.get_package(graph.paths.get(root, ''))}]\033[0m")
    for i, k in enumerate(sorted(graph.forward.get(root, []))): rec(k, "", i == len(graph.forward[root]) - 1)


def main():
    parser = argparse.ArgumentParser(description="inspect-deps: ELF dependency analyzer")
    parser.add_argument("elf", help="Target binary")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--tree", action="store_true")
    mode.add_argument("--pkg-list", action="store_true")
    mode.add_argument("--json", action="store_true")
    mode.add_argument("--why", metavar="LIB")
    mode.add_argument("--dot", metavar="FILE")
    parser.add_argument("--all-reasons", action="store_true")
    parser.add_argument("--show-stdlib", action="store_true")
    args = parser.parse_args()

    target = Path(args.elf).resolve()
    if not target.exists(): sys.exit("Error: File not found.")

    graph = DepGraph()
    graph.build(str(target), args.show_stdlib)
    root = target.name

    if args.json:
        data = {
            "root": root,
            "dependencies": {l: {"path": p, "pkg": SYS.get_package(p), "depth": graph.depth.get(l)} for l, p in
                             graph.paths.items()},
            "minimal_packages": get_minimal_pkgs(graph, root)
        }
        print(json.dumps(data, indent=2))
    elif args.tree:
        print_tree(graph, root)
    elif args.pkg_list:
        print(" ".join(get_minimal_pkgs(graph, root)))
    elif args.why:
        def dfs(cur, path):
            if cur == root: print(" -> ".join(path + [cur])); return
            for p in graph.reverse[cur]:
                if p not in path: dfs(p, path + [cur])

        dfs(args.why, [])
    elif args.dot:
        with open(args.dot, "w") as f:
            f.write("digraph deps {\n  rankdir=LR;\n")
            for p, kids in graph.forward.items():
                for k in kids: f.write(f'  "{p}" -> "{k}";\n')
            f.write("}\n")
    else:
        libs = sorted(graph.reverse)
        w = max(len(l) for l in libs) if libs else 10
        for l in libs:
            pkg = SYS.get_package(graph.paths.get(l, ""))
            print(
                f"{l.ljust(w)}  {pkg[:14].ljust(15)} {str(graph.depth.get(l, '?')).center(5)}  <- {sorted(graph.reverse[l])[0]}")


if __name__ == "__main__":
    main()
