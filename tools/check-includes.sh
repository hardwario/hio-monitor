#!/usr/bin/env bash
# Rejects includes of standard-library implementation headers (`#include <bits/...>`, libstdc++
# internals) in the tracked C++ sources: they build with libstdc++ only and break libc++ (macOS) and
# the MSVC STL (Windows) (ADR-0001; plan/features/0011 review finding B1). Include the public
# header instead (<chrono>, <string>, ...).
#
# Usage: tools/check-includes.sh
# Exit status: 0 when clean, 1 when there are offenders (listed on stderr).

set -eu

cd "$(git rev-parse --show-toplevel)"

offenders=$(git ls-files -z -- '*.h' '*.hpp' '*.cpp' '*.cxx' \
    | xargs -0 grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*<bits/' || true)
if [ -n "$offenders" ]; then
    echo "Standard-library implementation headers (<bits/...>) are not portable; include the" >&2
    echo "public header instead (ADR-0001):" >&2
    echo "$offenders" >&2
    exit 1
fi
echo "Include check passed."
