#!/usr/bin/env bash
# Checks the Git LFS rule from CLAUDE.md: every file that is binary or larger than 100 KB must be
# stored as a Git LFS pointer (its pattern belongs in .gitattributes), and every file matching an
# LFS pattern must really be a pointer. Vendor SDK binaries must never be committed at all.
#
# Usage: tools/check-lfs.sh [--untracked]
#   Checks the files in the index (what will be committed). With --untracked, untracked files that
#   are not ignored are checked as well (their working-tree content), e.g. before the first commit.
# Exit status: 0 when clean, 1 when there are offenders (listed on stderr), 2 on usage errors.

# No pipefail: `grep -q` / `head` may close a pipe early, which is not an error here.
set -eu

readonly limit_bytes=$((100 * 1024))
readonly lfs_pointer_header='version https://git-lfs.github.com/spec/v1'

check_untracked=false
case "${1:-}" in
    "") ;;
    --untracked) check_untracked=true ;;
    *)
        echo "usage: $0 [--untracked]" >&2
        exit 2
        ;;
esac

cd "$(git rev-parse --show-toplevel)"

offenders=()

# check_file <path> <in-index:true|false> <content-command...>: the command prints the content.
check_file() {
    local path=$1
    local in_index=$2
    shift 2
    local filter
    filter=$(git check-attr filter -- "$path" | sed 's/^.*: filter: //')
    if [[ "$filter" == "lfs" ]]; then
        # Untracked files become pointers when they are added (git lfs clean filter).
        if $in_index && ! "$@" | head -c 200 | grep -q "^${lfs_pointer_header}"; then
            offenders+=("$path: matches an LFS pattern in .gitattributes but is not an LFS pointer")
        fi
        return
    fi
    local size
    size=$("$@" | wc -c)
    if ((size > limit_bytes)); then
        offenders+=("$path: ${size} bytes > 100 KB; track it with Git LFS (.gitattributes)")
        return
    fi
    # grep -I treats binary content as non-matching; empty files are text.
    if ((size > 0)) && ! "$@" | LC_ALL=C grep -qI ''; then
        offenders+=("$path: binary file; track its type with Git LFS (.gitattributes)")
    fi
}

while IFS= read -r -d '' path; do
    # Skip submodules (gitlinks) and entries removed from the index.
    mode=$(git ls-files --stage -- "$path" | cut -d' ' -f1)
    [[ "$mode" == "160000" ]] && continue
    check_file "$path" true git cat-file blob ":$path"
done < <(git ls-files -z)

if $check_untracked; then
    while IFS= read -r -d '' path; do
        [[ -f "$path" ]] || continue
        check_file "$path" false cat -- "$path"
    done < <(git ls-files -z --others --exclude-standard)
fi

if ((${#offenders[@]} > 0)); then
    echo "Git LFS check failed (CLAUDE.md: large or binary files only via Git LFS):" >&2
    printf '  %s\n' "${offenders[@]}" >&2
    exit 1
fi
echo "Git LFS check passed."
