#!/usr/bin/env bash
# Rename the project prefix hio -> hwio / Hio -> HWio / HIO -> HWIO (plan/features/0024, ADR-0181).
#
# Usage (from the repository root, GNU sed and git required):
#   tools/rename-hwio.sh            # rename paths and contents
#   tools/rename-hwio.sh --dry-run  # only list the paths and files that would change
#
# Re-runnable: on an already renamed tree it changes nothing ("hwio", "HWio" and "HWIO" do not contain the
# old spellings). Meant to be run on branches that still use the old names (e.g. the CI repair branch)
# before they are merged. Afterwards: clang-format the C++ sources (include order), rebuild, run
# `update_translations` and the audit grep from plan/features/0024.
#
# Not touched:
#   - plan/decisions/ and plan/features/ (history; ADR-0181 and plan 0024 name the old prefix on purpose),
#   - Git LFS files and binary files (paths of LFS files are still renamed; their content is not),
#   - this script.
# Protected strings (masked before the substitutions and restored after):
#   - hardwario/hio-monitor     the GitHub repository (update check, release links, tests),
#   - hio-chester-*, hio-test-* firmware names (catalog, fixtures, fake fw-name in tests),
#   - ../hio-monitor*           sibling worktree / clone directories named in docs.
# Not matched by the rules at all: hiomon (the old app's process name), hioMockup (mock-up storage key),
# the product name "HARDWARIO Monitor", com.hardwario.monitor, the QML URI HardwarioMonitor.
set -euo pipefail

dry_run=0
if [[ "${1:-}" == "--dry-run" ]]; then
    dry_run=1
elif [[ $# -gt 0 ]]; then
    echo "usage: tools/rename-hwio.sh [--dry-run]" >&2
    exit 2
fi

if [[ ! -f CLAUDE.md || ! -d .git && ! -f .git ]]; then
    echo "rename-hwio: run from the repository root" >&2
    exit 2
fi
if ! sed --version 2>/dev/null | grep -q 'GNU sed'; then
    echo "rename-hwio: GNU sed is required" >&2
    exit 2
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "rename-hwio: warning: the work tree has uncommitted changes (they are renamed too)" >&2
fi

self="tools/rename-hwio.sh"

# Substitution program shared by paths and contents. Order matters: masks first, restores last.
# The word rule treats "_" as a separator, so _hio_x, hio_x and hio::x are all renamed.
# shellcheck disable=SC2016
sed_program='
s#hardwario/hio-monitor#@@RENAME_KEEP_REPO@@#g
s#hio-chester-#@@RENAME_KEEP_CHESTER@@#g
s#hio-test-#@@RENAME_KEEP_TESTFW@@#g
s#\.\./hio-monitor#@@RENAME_KEEP_SIBLING@@#g
s#HIO_#HWIO_#g
s#\bHIO\b#HWIO#g
s#Hio([A-Z])#HWio\1#g
s#libhio_#libhwio_#g
s#\\fBhio\b#\\fBhwio#g
:word
s#(^|[^A-Za-z0-9])hio($|[^A-Za-z0-9])#\1hwio\2#
t word
s#@@RENAME_KEEP_REPO@@#hardwario/hio-monitor#g
s#@@RENAME_KEEP_CHESTER@@#hio-chester-#g
s#@@RENAME_KEEP_TESTFW@@#hio-test-#g
s#@@RENAME_KEEP_SIBLING@@#../hio-monitor#g
'

rename_string() { printf '%s\n' "$1" | sed -E "$sed_program"; }

excluded() {
    case "$1" in
        plan/decisions/* | plan/features/* | "$self") return 0 ;;
    esac
    return 1
}

mapfile -t lfs_files < <(git lfs ls-files --name-only 2>/dev/null || true)
declare -A is_lfs=()
for f in "${lfs_files[@]}"; do is_lfs["$f"]=1; done

# --- 1. Paths --------------------------------------------------------------------------------------------
mapfile -t tracked < <(git ls-files)
mapfile -t untracked < <(git ls-files --others --exclude-standard)
declare -A is_tracked=()
for f in "${tracked[@]}"; do is_tracked["$f"]=1; done

moved=0
for f in "${tracked[@]}" "${untracked[@]}"; do
    excluded "$f" && continue
    [[ -e "$f" || -L "$f" ]] || continue # deleted in the work tree
    new="$(rename_string "$f")"
    [[ "$new" == "$f" ]] && continue
    moved=$((moved + 1))
    if ((dry_run)); then
        echo "mv  $f -> $new"
        continue
    fi
    if [[ -e "$new" ]]; then
        echo "rename-hwio: $new already exists (from $f)" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$new")"
    if [[ -n "${is_tracked[$f]:-}" ]]; then
        git mv "$f" "$new"
    else
        mv "$f" "$new"
    fi
    if [[ -n "${is_lfs[$f]:-}" ]]; then is_lfs["$new"]=1; fi
    # Remove directories the move left empty (e.g. include/hio/); stops at the first non-empty parent.
    rmdir -p --ignore-fail-on-non-empty "$(dirname "$f")" 2>/dev/null || true
done

# --- 2. Contents -----------------------------------------------------------------------------------------
mapfile -t files < <(git ls-files --cached --others --exclude-standard | sort -u)
changed=0
for f in "${files[@]}"; do
    excluded "$f" && continue
    [[ -f "$f" && ! -L "$f" ]] || continue
    [[ -n "${is_lfs[$f]:-}" ]] && continue
    grep -Iq . "$f" 2>/dev/null || continue # binary or empty
    grep -q -E 'hio|HIO|Hio' "$f" || continue
    if ((dry_run)); then
        if ! sed -E "$sed_program" "$f" | cmp -s - "$f"; then
            echo "sed $f"
            changed=$((changed + 1))
        fi
        continue
    fi
    tmp="$(mktemp)"
    sed -E "$sed_program" "$f" >"$tmp"
    if cmp -s "$tmp" "$f"; then
        rm -f "$tmp"
    else
        cat "$tmp" >"$f" # keeps the file mode
        rm -f "$tmp"
        changed=$((changed + 1))
    fi
done

echo "rename-hwio: $moved path(s) renamed, $changed file(s) rewritten$( ((dry_run)) && echo ' (dry run)')"
