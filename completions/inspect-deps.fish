complete -c inspect-deps -f
complete -c inspect-deps -s h -l help -d "Show this help message and exit"
complete -c inspect-deps -l tree -d "Show dependency tree"
complete -c inspect-deps -l pkg-list -d "Show list of packages"
complete -c inspect-deps -l json -d "Output in JSON format"
complete -c inspect-deps -l why -r -d "Explain why a library is needed"
complete -c inspect-deps -l dot -r -d "Generate DOT graph file"
complete -c inspect-deps -l all-reasons -d "Show all reasons for dependency"
complete -c inspect-deps -l show-stdlib -d "Show standard library dependencies"

