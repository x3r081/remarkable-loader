#!/usr/bin/env bash
# Cross-compile a project in this repo for the reMarkable 2.
#   ./tools/build.sh            -> builds switcher
#   ./tools/build.sh switcher   -> builds switcher
#   ./tools/build.sh launcher   -> builds launcher
set -euo pipefail

# Point SDK_ROOT at your installed Codex SDK, or export SDK_ENV directly.
SDK_ROOT="${SDK_ROOT:-$HOME/codex-sdk/rm2/5.7.119}"
SDK_ENV="${SDK_ENV:-$SDK_ROOT/environment-setup-cortexa7hf-neon-remarkable-linux-gnueabi}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${1:-switcher}"
PROJECT_DIR="$REPO/$PROJECT"

if [[ ! -f "$SDK_ENV" ]]; then
    echo "SDK environment script not found: $SDK_ENV" >&2
    exit 1
fi
# The SDK script references unbound vars internally; relax while sourcing it.
set +u
# shellcheck disable=SC1090
source "$SDK_ENV"
set -u

if [[ "$PROJECT" == "switcher" ]]; then
    # Plain C, no Qt: the gesture daemon and the test injectors.
    mkdir -p "$PROJECT_DIR/build"
    $CC -O2 -Wall "$PROJECT_DIR/gesture.c" "$PROJECT_DIR/modeswitchd.c" \
        -o "$PROJECT_DIR/build/modeswitchd"
    $CC -O2 -Wall "$PROJECT_DIR/uinject.c" -o "$PROJECT_DIR/build/uinject"
    $CC -O2 -Wall "$PROJECT_DIR/peninject.c" -o "$PROJECT_DIR/build/peninject"
    file "$PROJECT_DIR/build/"*
    exit 0
fi

if [[ ! -f "$PROJECT_DIR/CMakeLists.txt" ]]; then
    echo "No such project: $PROJECT_DIR" >&2
    exit 1
fi

cmake -S "$PROJECT_DIR" -B "$PROJECT_DIR/build"
cmake --build "$PROJECT_DIR/build" -j"$(nproc)"

echo
find "$PROJECT_DIR/build" -maxdepth 1 -type f -executable -exec file {} \;
