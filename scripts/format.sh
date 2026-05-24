#!/usr/bin/env bash
# Format all C++ source files in misa77 per .clang-format.
# Run from anywhere; resolves the project root from the script's own location.

set -euo pipefail

cd "$(dirname "$0")/.."

find src -type f \
    \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    -exec clang-format -i {} +

echo "Formatted."
