#!/usr/bin/env bash

# Usage:
#   ./clang_format.sh check                 # check every eligible file under source/
#   ./clang_format.sh apply                 # reformat every eligible file under source/
#   ./clang_format.sh check  file [file...] # check only the given files
#   ./clang_format.sh apply  file [file...] # reformat only the given files
#
# The explicit-file form is used by CI to gate only the files a pull request
# touches. Files given on the command line are put through the same extension
# and pruned-directory rules as the full-tree walk, so this script stays the
# single owner of those rules.
#
# The style config lives at the repository root (.clang-format) so that
# clang-format's `--style=file` upward search finds it from any file in the
# tree. Do not move it back under scripts/: from a file in source/ the search
# would never reach it and clang-format would silently fall back to its
# built-in LLVM defaults.

set -euo pipefail

MODE="${1:-}"

if [[ "$MODE" != "check" && "$MODE" != "apply" ]]; then
    echo "Usage: $0 {check|apply} [file...]"
    exit 2
fi
shift || true

# 1. Resolve absolute path of the script and its sibling 'source' folder
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SOURCE_ROOT="$PROJECT_ROOT/source"

if [ ! -d "$SOURCE_ROOT" ]; then
    echo "Error: Source directory not found at $SOURCE_ROOT"
    exit 1
fi

# Directories under source/ that are never formatted, and the extensions that are.
PRUNED_DIRS=(external build_docker samples docs docker)
EXTENSIONS=(c h cpp hpp cc hh cxx hxx)

# collect_all: every eligible file under source/, NUL separated.
collect_all() {
    local find_args=()
    local dir
    for dir in "${PRUNED_DIRS[@]}"; do
        find_args+=(-path "$SOURCE_ROOT/$dir" -prune -o)
    done

    local name_args=()
    local ext
    for ext in "${EXTENSIONS[@]}"; do
        name_args+=(-o -name "*.$ext")
    done
    name_args=("${name_args[@]:1}")  # drop the leading -o

    find "$SOURCE_ROOT" "${find_args[@]}" -type f \( "${name_args[@]}" \) -print0
}

# collect_given: the caller's files, minus anything the full-tree walk would
# have skipped (wrong extension, outside source/, or inside a pruned dir).
# Deleted files are dropped too, so a PR that removes a file still passes.
collect_given() {
    local file abs rel ext dir keep
    for file in "$@"; do
        [ -f "$file" ] || continue

        abs="$(cd "$(dirname "$file")" && pwd)/$(basename "$file")"

        case "$abs" in
            "$SOURCE_ROOT"/*) rel="${abs#"$SOURCE_ROOT"/}" ;;
            *) continue ;;
        esac

        keep=0
        for ext in "${EXTENSIONS[@]}"; do
            [ "${abs##*.}" = "$ext" ] && keep=1 && break
        done
        [ "$keep" = 1 ] || continue

        for dir in "${PRUNED_DIRS[@]}"; do
            case "$rel" in
                "$dir"/*) keep=0 ;;
            esac
        done
        [ "$keep" = 1 ] || continue

        printf '%s\0' "$abs"
    done
}

if [ "$#" -gt 0 ]; then
    SCOPE="the $# path(s) given on the command line"
    collect() { collect_given "$@"; }
else
    SCOPE="$SOURCE_ROOT"
    collect() { collect_all; }
fi

FILES=()
while IFS= read -r -d '' file; do
    FILES+=("$file")
done < <(collect "$@")

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "No files to format in $SCOPE."
    exit 0
fi

# 2. Execution
if [ "$MODE" = "check" ]; then
    echo "Checking formatting in $SCOPE..."

    # --dry-run --Werror reports the positions that are wrong; the unified diff
    # below shows the contributor exactly what the formatter wanted instead.
    if clang-format --style=file --dry-run --Werror "${FILES[@]}"; then
        echo "All files correctly formatted."
        exit 0
    else
        echo "[!] Formatting errors found! Proposed changes:"
        for file in "${FILES[@]}"; do
            rel="${file#"$PROJECT_ROOT"/}"
            diff -u --label "a/$rel" --label "b/$rel" \
                "$file" <(clang-format --style=file "$file") || true
        done
        exit 1
    fi
else
    echo "Applying formatting in $SCOPE..."
    clang-format -i --style=file "${FILES[@]}"
    echo "Formatting complete."
fi
