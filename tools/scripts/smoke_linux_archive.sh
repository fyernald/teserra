#!/usr/bin/env bash
# Unpacks a Linux examples archive (#317) and runs every example in it for a few frames, as a tester would run
# them, so a library linked instead of opened at run time, or a missing asset, fails the release job.
#
#   tools/scripts/smoke_linux_archive.sh dist/teserra-0.2.0-linux-x64-examples.tar.gz
#
# Needs a display: run it under xvfb-run (with Mesa, GALLIUM_DRIVER=llvmpipe on machines without a GPU). An
# example fails if it exits with another code than 0 or logs at error level or above.
set -euo pipefail

archive=${1:?usage: smoke_linux_archive.sh <examples archive>}
frames=${FRAMES:-60}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

tar -xzf "$archive" -C "$work"
root=$(find "$work" -mindepth 1 -maxdepth 1 -type d)
failed=0
for folder in "$root"/*/; do
    example=$(basename "$folder")
    executable="$folder/teserra_$example"
    if [[ ! -x "$executable" ]]; then
        echo "::error::$example: teserra_$example is missing or not executable"
        failed=1
        continue
    fi
    # From its own folder, as the README says, so assets/ resolves next to the executable.
    if output=$(cd "$folder" && "./teserra_$example" --frames "$frames" --hidden-window --no-audio 2>&1) &&
        ! grep -qE "\[(error|critical)\]" <<< "$output"; then
        echo "ok: $example"
    else
        echo "::error::$example failed"
        echo "$output" | tail -n 30
        failed=1
    fi
done
exit "$failed"
