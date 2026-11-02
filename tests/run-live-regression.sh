#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
# No game assets are bundled. Pass the directory containing your legal assets.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
GAME=${1:-"$ROOT/build/pvz-portable"}
GAME=$(realpath -- "$GAME")
RESOURCES=${2:-"$(dirname -- "$GAME")"}
RESOURCES=$(realpath -- "$RESOURCES")
[[ -x "$GAME" ]] || { printf 'Not executable: %s\n' "$GAME" >&2; exit 2; }
# The app is destructive in test mode: always isolate saves and mods, even if
# the caller forgot. Keep logs/HOME after the run for debugging; delete nothing.
TEST_HOME=$(mktemp -d "${TMPDIR:-/tmp}/pvzp-live.XXXXXX")
MODS="$TEST_HOME/.local/share/io.github.wszqkzqk/PvZPortable/mods"
mkdir -p -- "$MODS"
for pack in 70-box-nuts 80-brain 90-aurora-nut 95-bounty-zombies 100-prism-megaphone 110-composite-zombies 120-custom-campaign; do
  cp -a -- "$ROOT/tests/mods/$pack" "$MODS/"
done
printf 'Disposable test HOME and log directory: %s\n' "$TEST_HOME"
(
  cd -- "$ROOT"
  PVZP_MOD_TEST=1 "$GAME" > "$TEST_HOME/mod-tests.log" 2>&1
)
MODE=${3:-custom}
export HOME="$TEST_HOME" SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy
# Exactly one live driver can own a Board. Ignore inherited developer hooks.
unset PVZP_DEV_CHOOSER PVZP_DEV_UNLOCK PVZP_GAMEPLAY_TEST PVZP_ELECTRIC_TEST PVZP_TEST_LANGUAGE PVZP_LEVEL_TEST PVZP_CUSTOM_TEST
case "$MODE" in
  custom) export PVZP_CUSTOM_TEST=1 ;;
  levels) export PVZP_LEVEL_TEST=1 ;;
  *) printf 'Unknown test mode: %s (custom or levels)\n' "$MODE" >&2; exit 2 ;;
esac
run=(timeout 240 "$GAME" -resdir "$RESOURCES")
if [[ ${PVZP_CUSTOM_HOLD:-0} == 1 ]]; then run=("$GAME" -resdir "$RESOURCES"); fi
set +e
if [[ -n ${DISPLAY:-} ]]; then
  "${run[@]}" > "$TEST_HOME/live-tests.log" 2>&1
else
  xvfb-run -a -s '-screen 0 1400x900x24' "${run[@]}" > "$TEST_HOME/live-tests.log" 2>&1
fi
result=$?
set -e
# Print only test summaries; detailed resource loading warnings stay in the log.
python3 - "$TEST_HOME/mod-tests.log" "$TEST_HOME/live-tests.log" <<'PY'
import pathlib, sys
for name in sys.argv[1:]:
    for line in pathlib.Path(name).read_text(errors='replace').splitlines():
        if 'RESULT' in line or 'TEST FAIL' in line:
            print(line)
PY
exit "$result"
