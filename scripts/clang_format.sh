#!/bin/bash

# Check if NOEDIT flag is passed
NOEDIT=false
if [[ "$1" == "NOEDIT" ]]; then
    NOEDIT=true
fi

for dir in src include examples; do
    if $NOEDIT; then
    # Just check formatting, don't modify files
        find "$dir" -type f \( -name "*.cpp" -o -name "*.h" \) -exec clang-format --dry-run --Werror {} +
    else
    # Actually format files
        find "$dir" -type f \( -name "*.cpp" -o -name "*.h" \) -exec clang-format -i {} +
    fi
done
