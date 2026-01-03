#include <print>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <deque>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <optional>
#include <ranges>
#include <span>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <glaze/glaze.hpp>
#include <elfio/elfio.hpp>
#include <dlfcn.h>

namespace fs = std::filesystem;
namespace r = std::ranges;
namespace v = std::ranges::views;

constexpr std::array<std::string_view, 2> NOISE_PREFIX = {"linux-vdso", "ld-linux"};
constexpr std::array<std::string_view, 5> GLIBC_PREFIX = {
    "libc.so", "libm.so", "libpthread.so", "librt.so", "libdl.so"
};

// ALPM Types (copied from alpm.h)
extern "C" {
// NOLINTBEGIN(bugprone-reserved-identifier)
typedef struct __alpm_handle_t alpm_handle_t;
typedef struct __alpm_db_t alpm_db_t;
typedef struct __alpm_pkg_t alpm_pkg_t;

typedef enum _alpm_errno_t
{
    ALPM_ERR_OK = 0
} alpm_errno_t;

typedef struct __alpm_list_t
{
    void* data;
    __alpm_list_t* prev;
    __alpm_list_t* next;
} alpm_list_t;

typedef struct _alpm_file_t
{
    char* name;
    off_t size;
    mode_t mode;
} alpm_file_t;

typedef struct _alpm_filelist_t
{
    size_t count;
    alpm_file_t* files;
} alpm_filelist_t;

// NOLINTEND(bugprone-reserved-identifier)

// Function pointer types
typedef alpm_handle_t* (*alpm_initialize_fn)(const char*, const char*, alpm_errno_t*);
typedef int (*alpm_release_fn)(alpm_handle_t*);
typedef alpm_db_t* (*alpm_get_localdb_fn)(alpm_handle_t*);
typedef const char* (*alpm_strerror_fn)(alpm_errno_t);
typedef alpm_list_t* (*alpm_db_get_pkgcache_fn)(alpm_db_t*);
typedef alpm_list_t* (*alpm_list_next_fn)(const alpm_list_t*);
typedef alpm_filelist_t* (*alpm_pkg_get_files_fn)(alpm_pkg_t*);
typedef const char* (*alpm_pkg_get_name_fn)(alpm_pkg_t*);
}

class LdCache
{
    struct HeaderNew
    {
        char magic[20];
        uint32_t nlibs;
        uint32_t len_strings;
        uint32_t unused[5];
    };

    struct EntryNew
    {
        int32_t flags;
        uint32_t key;
        uint32_t value;
        uint32_t osversion;
        uint64_t hwcap;
    };

    std::unordered_map<std::string_view, std::string_view> cache;
    void* mmap_addr = MAP_FAILED;
    size_t mmap_size = 0;

public:
    LdCache()
    {
        const int fd = open("/etc/ld.so.cache", O_RDONLY);
        if (fd == -1) return;

        struct stat st{};
        if (fstat(fd, &st) == 0)
        {
            mmap_size = st.st_size;
            mmap_addr = mmap(nullptr, mmap_size, PROT_READ, MAP_PRIVATE, fd, 0);
        }
        close(fd);

        if (mmap_addr == MAP_FAILED) return;

        if (mmap_size < sizeof(HeaderNew)) return;
        auto* header = static_cast<HeaderNew*>(mmap_addr);

        std::string_view magic(header->magic, 17);
        if (magic == "glibc-ld.so.cache" && header->magic[17] == '1')
        {
            size_t offset_strings = sizeof(HeaderNew) + header->nlibs * sizeof(EntryNew);
            if (offset_strings > mmap_size) return;

            auto base_addr = static_cast<const char*>(mmap_addr);
            const auto entries_span = std::span(
                reinterpret_cast<EntryNew*>(static_cast<char*>(mmap_addr) + sizeof(HeaderNew)),
                header->nlibs
            );

            for (const auto& entry : entries_span)
            {
                if (entry.key >= mmap_size || entry.value >= mmap_size) continue;
                cache.emplace(base_addr + entry.key, base_addr + entry.value);
            }
        }
    }

    LdCache(const LdCache&) = delete;
    LdCache& operator=(const LdCache&) = delete;

    ~LdCache()
    {
        if (mmap_addr != MAP_FAILED) munmap(mmap_addr, mmap_size);
    }

    std::optional<std::string> resolve(const std::string& soname)
    {
        if (const auto it = cache.find(soname); it != cache.end())
        {
            return std::string(it->second);
        }
        return std::nullopt;
    }
};

class AlpmManager
{
    void* lib_handle = nullptr;
    alpm_handle_t* handle = nullptr;
    alpm_db_t* db_local = nullptr;
    std::unordered_map<std::string, std::string> pkg_cache;
    bool initialized = false;

    // Function pointers
    alpm_initialize_fn _alpm_initialize = nullptr;
    alpm_release_fn _alpm_release = nullptr;
    alpm_get_localdb_fn _alpm_get_localdb = nullptr;
    alpm_strerror_fn _alpm_strerror = nullptr;
    alpm_db_get_pkgcache_fn _alpm_db_get_pkgcache = nullptr;
    alpm_list_next_fn _alpm_list_next = nullptr;
    alpm_pkg_get_files_fn _alpm_pkg_get_files = nullptr;
    alpm_pkg_get_name_fn _alpm_pkg_get_name = nullptr;

    template <typename T>
    static T sym(void* handle, const char* name)
    {
        return reinterpret_cast<T>(dlsym(handle, name));
    }

public:
    AlpmManager()
    {
        lib_handle = dlopen("libalpm.so", RTLD_LAZY);
        if (!lib_handle)
        {
            lib_handle = dlopen("libalpm.so.13", RTLD_LAZY);
        }
        if (!lib_handle)
        {
            lib_handle = dlopen("libalpm.so.14", RTLD_LAZY);
        }

        if (!lib_handle) return;

        _alpm_initialize = sym<alpm_initialize_fn>(lib_handle, "alpm_initialize");
        _alpm_release = sym<alpm_release_fn>(lib_handle, "alpm_release");
        _alpm_get_localdb = sym<alpm_get_localdb_fn>(lib_handle, "alpm_get_localdb");
        _alpm_strerror = sym<alpm_strerror_fn>(lib_handle, "alpm_strerror");
        _alpm_db_get_pkgcache = sym<alpm_db_get_pkgcache_fn>(lib_handle, "alpm_db_get_pkgcache");
        _alpm_list_next = sym<alpm_list_next_fn>(lib_handle, "alpm_list_next");
        _alpm_pkg_get_files = sym<alpm_pkg_get_files_fn>(lib_handle, "alpm_pkg_get_files");
        _alpm_pkg_get_name = sym<alpm_pkg_get_name_fn>(lib_handle, "alpm_pkg_get_name");

        if (!_alpm_initialize || !_alpm_release || !_alpm_get_localdb || !_alpm_strerror ||
            !_alpm_db_get_pkgcache || !_alpm_list_next || !_alpm_pkg_get_files || !_alpm_pkg_get_name)
        {
            std::println(std::cerr, "Failed to load ALPM symbols.");
            dlclose(lib_handle);
            lib_handle = nullptr;
            return;
        }

        alpm_errno_t err;
        handle = _alpm_initialize("/", "/var/lib/pacman", &err);
        if (handle)
        {
            db_local = _alpm_get_localdb(handle);
            initialized = true;
        }
        else
        {
            std::println(std::cerr, "Failed to initialize alpm: {}", _alpm_strerror(err));
        }
    }

    AlpmManager(const AlpmManager&) = delete;
    AlpmManager& operator=(const AlpmManager&) = delete;

    ~AlpmManager()
    {
        if (handle && _alpm_release) _alpm_release(handle);
        if (lib_handle) dlclose(lib_handle);
    }

    void batch_resolve(const std::vector<std::string>& paths)
    {
        if (!initialized || !db_local) return;

        std::vector<std::string> to_find;
        for (const auto& p : paths)
        {
            if (!pkg_cache.contains(p)) to_find.push_back(p);
        }
        if (to_find.empty()) return;

        std::unordered_map<std::string, std::string> lookup_map;
        for (const auto& p : to_find)
        {
            if (p.starts_with('/'))
            {
                lookup_map[p.substr(1)] = p;
            }
            else
            {
                lookup_map[p] = p;
            }
        }

        const alpm_list_t* pkg_cache_list = _alpm_db_get_pkgcache(db_local);
        for (const alpm_list_t* i = pkg_cache_list; i; i = _alpm_list_next(i))
        {
            auto* pkg = static_cast<alpm_pkg_t*>(i->data);
            const alpm_filelist_t* files = _alpm_pkg_get_files(pkg);

            for (size_t f = 0; f < files->count; ++f)
            {
                const char* filename = files->files[f].name;
                if (auto it = lookup_map.find(filename); it != lookup_map.end())
                {
                    pkg_cache[it->second] = _alpm_pkg_get_name(pkg);
                    lookup_map.erase(it);
                    if (lookup_map.empty()) return;
                }
            }
        }
    }

    std::string get_package(const std::string& path)
    {
        if (const auto it = pkg_cache.find(path); it != pkg_cache.end())
        {
            return it->second;
        }
        return "-";
    }

    bool is_available() const { return initialized; }
};

struct Node
{
    std::string path;
    std::string pkg;
    int depth = 0;
    std::vector<std::string> children;
    std::vector<std::string> parents;
};

struct DepGraph
{
    std::unordered_map<std::string, Node> nodes;
    std::string root_name;
    LdCache ld_cache;
    AlpmManager alpm;

    void build(const std::string& root_path, bool show_stdlib, bool resolve_packages = true)
    {
        root_name = fs::path(root_path).filename().string();
        nodes[root_name] = {root_path, "", 0, {}, {}};

        std::deque<std::string> q;
        q.push_back(root_name);

        while (!q.empty())
        {
            std::string cur = q.front();
            q.pop_front();

            std::string cur_path = nodes[cur].path;
            if (cur_path.empty()) continue;

            ELFIO::elfio reader;
            if (!reader.load(cur_path)) continue;

            std::vector<std::string> needed;

            ELFIO::section* dyn_sec = reader.sections[".dynamic"];
            if (!dyn_sec) continue;

            ELFIO::dynamic_section_accessor dyn(reader, dyn_sec);
            ELFIO::Elf_Xword dyn_no = dyn.get_entries_num();
            for (ELFIO::Elf_Xword i = 0; i < dyn_no; ++i)
            {
                ELFIO::Elf_Xword tag;
                ELFIO::Elf_Xword value;
                std::string str;
                dyn.get_entry(i, tag, value, str);
                if (tag == ELFIO::DT_NEEDED)
                {
                    needed.push_back(str);
                }
            }

            for (const auto& lib : needed)
            {
                if (r::any_of(NOISE_PREFIX, [&](const auto& p) { return lib.starts_with(p); })) continue;
                if (!show_stdlib && r::any_of(GLIBC_PREFIX, [&](const auto& p) { return lib.starts_with(p); }))
                    continue;

                if (r::find(nodes[cur].children, lib) == nodes[cur].children.end())
                {
                    nodes[cur].children.push_back(lib);
                }

                if (!nodes.contains(lib))
                {
                    nodes[lib] = {"", "", nodes[cur].depth + 1, {}, {}};
                    nodes[lib].parents.push_back(cur);

                    if (auto res = ld_cache.resolve(lib))
                    {
                        nodes[lib].path = *res;
                        q.push_back(lib);
                    }
                }
                else
                {
                    if (r::find(nodes[lib].parents, cur) == nodes[lib].parents.end())
                    {
                        nodes[lib].parents.push_back(cur);
                    }
                }
            }
        }

        if (resolve_packages)
        {
            std::vector<std::string> all_paths;
            for (const auto& n : nodes | std::views::values)
            {
                if (!n.path.empty()) all_paths.push_back(n.path);
            }
            alpm.batch_resolve(all_paths);
            for (auto& n : nodes | std::views::values)
            {
                if (!n.path.empty()) n.pkg = alpm.get_package(n.path);
            }
        }
    }

    std::vector<std::string> get_minimal_pkgs()
    {
        std::unordered_map<std::string, std::string> lib_to_pkg;
        for (const auto& [l, n] : nodes)
        {
            if (!n.pkg.empty() && n.pkg != "-") lib_to_pkg[l] = n.pkg;
        }

        std::unordered_map<std::string, std::unordered_set<std::string>> pkg_deps;

        for (const auto& [parent, node] : nodes)
        {
            std::string p_pkg = lib_to_pkg.contains(parent)
                                    ? lib_to_pkg[parent]
                                    : (parent == root_name ? "__ROOT__" : "");
            if (p_pkg.empty()) continue;

            for (const auto& child : node.children)
            {
                if (lib_to_pkg.contains(child))
                {
                    std::string c_pkg = lib_to_pkg[child];
                    if (c_pkg != p_pkg) pkg_deps[p_pkg].insert(c_pkg);
                }
            }
        }

        std::unordered_set<std::string> transitive;
        for (const auto& [p, kids] : pkg_deps)
        {
            if (p != "__ROOT__") transitive.insert(kids.begin(), kids.end());
        }

        std::vector<std::string> result;
        if (pkg_deps.contains("__ROOT__"))
        {
            for (const auto& p : pkg_deps["__ROOT__"])
            {
                if (!transitive.contains(p)) result.push_back(p);
            }
        }
        r::sort(result);
        return result;
    }
};

struct JsonOutput
{
    std::string root;
    std::map<std::string, std::map<std::string, std::string>> dependencies;
    std::vector<std::string> minimal_packages;
};

void print_tree(const DepGraph& g, const std::string& root, const bool show_pkgs, const bool use_color)
{
    std::string gray = use_color ? "\033[90m" : "";
    std::string reset = use_color ? "\033[0m" : "";

    std::unordered_set<std::string> seen;
    auto rec = [&](auto&& self, const std::string& n, std::string pref, const bool last) -> void
    {
        const auto& node = g.nodes.at(n);
        std::print("{}{} {}", pref, (last ? "└── " : "├── "), n);

        if (show_pkgs)
        {
            std::print(" {}[{}]", gray, (node.pkg.empty() ? "-" : node.pkg));
            if (use_color) std::print("{}", reset);
        }
        std::println("");

        if (seen.contains(n)) return;
        seen.insert(n);

        size_t i = 0;
        std::vector<std::string> sorted_children = node.children;
        r::sort(sorted_children);

        for (const auto& child : sorted_children)
        {
            self(self, child, pref + (last ? "    " : "│   "), i == sorted_children.size() - 1);
            i++;
        }
    };

    const auto& root_node = g.nodes.at(root);
    std::print("{}", root);
    if (show_pkgs)
    {
        std::print(" {}[{}]", gray, (root_node.pkg.empty() ? "-" : root_node.pkg));
        if (use_color) std::print("{}", reset);
    }
    std::println("");

    size_t i = 0;
    std::vector<std::string> sorted_children = root_node.children;
    r::sort(sorted_children);
    for (const auto& child : sorted_children)
    {
        rec(rec, child, "", i == sorted_children.size() - 1);
        i++;
    }
}

void explain_why(const DepGraph& g, const std::string& target)
{
    if (!g.nodes.contains(target))
    {
        std::println(std::cerr, "Library {} not found in dependency graph.", target);
        return;
    }

    auto dfs = [&](auto&& self, const std::string& cur, std::vector<std::string>& path) -> void
    {
        if (cur == g.root_name)
        {
            path.push_back(cur);
            for (const auto& p : v::reverse(path)) std::print("{} -> ", p);
            std::println("{}", target);
            path.pop_back();
            return;
        }

        path.push_back(cur);
        for (const auto& p : g.nodes.at(cur).parents)
        {
            if (r::find(path, p) == path.end())
            {
                self(self, p, path);
            }
        }
        path.pop_back();
    };

    std::vector<std::string> path;
    for (const auto& p : g.nodes.at(target).parents)
    {
        dfs(dfs, p, path);
    }
}

void generate_completions(const CLI::App& app, const std::string& shell)
{
    if (shell == "fish")
    {
        std::println("# fish completion for inspect-deps");

        for (const auto* opt : app.get_options())
        {
            if (opt->get_name().empty()) continue;

            std::string short_opt;
            std::string long_opt;

            for (const auto& name : opt->get_snames()) short_opt = name;
            for (const auto& name : opt->get_lnames()) long_opt = name;

            std::print("complete -c inspect-deps");
            if (!short_opt.empty()) std::print(" -s {}", short_opt);
            if (!long_opt.empty()) std::print(" -l {}", long_opt);
            std::print(" -d \"{}\"", opt->get_description());

            if (long_opt == "completions")
            {
                std::print(" -a \"bash zsh fish\"");
            }
            else if (opt->get_expected() > 0)
            {
                std::print(" -r");
            }

            std::println("");
        }

        std::println("complete -c inspect-deps -a \"(__fish_complete_path)\"");
    }
    else if (shell == "zsh")
    {
        std::println("#compdef inspect-deps=inspect-deps");
        std::println("_arguments -s \\");

        for (const auto* opt : app.get_options())
        {
            if (opt->get_name().empty()) continue;

            std::string short_opt;
            std::string long_opt;
            for (const auto& name : opt->get_snames()) short_opt = name;
            for (const auto& name : opt->get_lnames()) long_opt = name;

            std::print("    '");
            if (!short_opt.empty()) std::print("-{}", short_opt);
            if (!long_opt.empty())
            {
                if (!short_opt.empty()) std::print(",");
                std::print("--{}", long_opt);
                if (opt->get_expected() > 0) std::print("=");
            }

            std::print("[{}]", opt->get_description());

            if (long_opt == "completions")
            {
                std::print(":completion:(bash zsh fish)");
            }
            else if (opt->get_expected() > 0)
            {
                std::print(":file:_files");
            }
            std::println("' \\");
        }
        std::println("    '*:elf file:_files'");
    }
    else if (shell == "bash")
    {
        std::println("# bash completion for inspect-deps");
        std::println("_inspect_deps() {{");
        std::println("    local cur prev opts");
        std::println("    COMPREPLY=()");
        std::println("    cur=\"${{COMP_WORDS[COMP_CWORD]}}\"");
        std::println("    prev=\"${{COMP_WORDS[COMP_CWORD-1]}}\"");

        std::print("    opts=\"");
        for (const auto* opt : app.get_options())
        {
            for (const auto& name : opt->get_snames()) std::print("-{} ", name);
            for (const auto& name : opt->get_lnames()) std::print("--{} ", name);
        }
        std::println("\"");

        std::println("    if [[ ${{prev}} == \"--completions\" ]]; then");
        std::println("        COMPREPLY=( $(compgen -W \"bash zsh fish\" -- ${{cur}}) )");
        std::println("        return 0");
        std::println("    fi");

        std::println("    if [[ ${{cur}} == -* ]]; then");
        std::println("        COMPREPLY=( $(compgen -W \"${{opts}}\" -- ${{cur}}) )");
        std::println("        return 0");
        std::println("    fi");
        std::println("    COMPREPLY=( $(compgen -f -- ${{cur}}) )");
        std::println("}}");
        std::println("complete -F _inspect_deps inspect-deps");
    }
}

int main(int argc, char** argv)
{
    CLI::App app{"inspect-deps: ELF dependency analyzer"};

    std::string elf_path;
    app.add_option("elf", elf_path, "Target binary");

    bool show_tree = false;
    bool show_json = false;
    bool show_pkg_list = false;
    bool show_stdlib = false;
    bool no_header = false;
    bool show_dot = false;
    bool no_pkg = false;
    std::string why_lib;
    std::string completion_shell;

    auto* mode = app.add_option_group("Mode");
    mode->add_flag("--tree", show_tree, "Show dependency tree");
    mode->add_flag("--json", show_json, "Output in JSON format");
    mode->add_flag("--pkg-list", show_pkg_list, "Show list of packages (Arch Linux only)");
    mode->add_option("--why", why_lib, "Explain why a library is needed");
    mode->add_flag("--dot", show_dot, "Output DOT graph");

    app.add_option("--completions", completion_shell, "Generate shell completions (bash, zsh, fish)")
       ->option_text("SHELL");

    app.add_flag("--show-stdlib", show_stdlib, "Show standard library dependencies");
    app.add_flag("--no-header", no_header, "Disable output header");
    app.add_flag("--no-pkg", no_pkg, "Disable package resolution");

    CLI11_PARSE(app, argc, argv);

    if (!completion_shell.empty())
    {
        generate_completions(app, completion_shell);
        return 0;
    }

    if (elf_path.empty())
    {
        std::println(std::cerr, "Error: Target binary is required.");
        std::println("{}", app.help());
        return 1;
    }

    if (!fs::exists(elf_path))
    {
        std::println(std::cerr, "Error: File not found.");
        return 1;
    }

    bool use_color = isatty(fileno(stdout));

    DepGraph graph;
    graph.build(fs::absolute(elf_path).string(), show_stdlib, !no_pkg);

    if (show_json)
    {
        std::map<std::string, std::map<std::string, std::string>> out_deps;
        for (const auto& [k, n] : graph.nodes)
        {
            out_deps[k] = {
                {"path", n.path},
                {"pkg", n.pkg},
                {"depth", std::to_string(n.depth)}
            };
        }

        auto minimal = graph.get_minimal_pkgs();

        JsonOutput out{graph.root_name, out_deps, minimal};
        std::string buffer;
        if (glz::write_json(out, buffer))
        {
            std::println(std::cerr, "Error writing JSON");
        }
        std::println("{}", buffer);
    }
    else if (show_tree)
    {
        print_tree(graph, graph.root_name, !no_pkg && graph.alpm.is_available(), use_color);
    }
    else if (show_pkg_list)
    {
        if (!graph.alpm.is_available())
        {
            std::println(std::cerr, "Error: libalpm not loaded. Cannot resolve packages.");
            return 1;
        }
        auto pkgs = graph.get_minimal_pkgs();
        for (size_t i = 0; i < pkgs.size(); ++i)
        {
            std::print("{}{}", pkgs[i], (i == pkgs.size() - 1 ? "" : " "));
        }
        std::println("");
    }
    else if (!why_lib.empty())
    {
        explain_why(graph, why_lib);
    }
    else if (show_dot)
    {
        std::println("digraph deps {{");
        std::println("  rankdir=LR;");
        for (const auto& [p, n] : graph.nodes)
        {
            for (const auto& c : n.children)
            {
                std::println(R"(  "{}" -> "{}";)", p, c);
            }
        }
        std::println("}}");
    }
    else
    {
        size_t w = 0;
        for (const auto& k : graph.nodes | std::views::keys) w = std::max(w, k.length());

        bool show_pkgs = graph.alpm.is_available() && !no_pkg;

        std::string bold = use_color ? "\033[1m" : "";
        std::string reset = use_color ? "\033[0m" : "";

        if (!no_header)
        {
            if (show_pkgs)
            {
                std::println("{}{:<{}}  {:<16} {:<6} {}{}", bold, "Library", w + 2, "Package", "Depth", "Required By",
                             reset);
            }
            else
            {
                std::println("{}{:<{}}  {:<6} {}{}", bold, "Library", w + 2, "Depth", "Required By", reset);
            }
        }

        std::vector<std::string> sorted_keys;
        for (const auto& k : graph.nodes | std::views::keys) sorted_keys.push_back(k);
        r::sort(sorted_keys);

        for (const auto& k : sorted_keys)
        {
            const auto& n = graph.nodes.at(k);
            std::string parent = n.parents.empty() ? "-" : n.parents[0];
            if (n.parents.size() > 1) parent += " (+)";

            if (show_pkgs)
            {
                std::string pkg_str = n.pkg.empty() ? "-" : n.pkg;
                std::println("{:<{}}  {:<16} {:<6} {}", k, w + 2, pkg_str.substr(0, 14), n.depth, parent);
            }
            else
            {
                std::println("{:<{}}  {:<6} {}", k, w + 2, n.depth, parent);
            }
        }
    }

    return 0;
}
