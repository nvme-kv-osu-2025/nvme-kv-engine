#!/bin/bash
set -e 

# Check if NOEDIT flag is passed
NOEDIT=false
if [[ "$1" == "NOEDIT" ]]; then
    NOEDIT=true
fi

for dir in src include examples; do
    if $NOEDIT; then
        # Just check formatting, don't modify files
        for file in $(find "$dir" -type f \( -name "*.cpp" -o -name "*.h" \)); do
            clang-format --dry-run --Werror "$file"
        done
    else
        # Actually format files
        find "$dir" -type f \( -name "*.cpp" -o -name "*.h" \) -exec clang-format -i {} +
    fi
done
