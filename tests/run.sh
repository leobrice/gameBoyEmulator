#!/bin/sh
# Command line checks: the emulator has to work with no rom at all, and it has
# to work when handed the same rom as a file.
set -eu

BIN=${1:-./gameboy}
OUT=$(mktemp)
PPM=$(mktemp)
trap 'rm -f "$OUT" "$PPM"' EXIT

check_frame() {
	distinct=$(od -An -v -tu1 -j15 "$PPM" | tr -s ' ' '\n' | grep . | sort -u | wc -l)
	if [ "$distinct" -lt 4 ]; then
		echo "FAIL: $1 frame has $distinct distinct shades, expected 4"
		exit 1
	fi
	echo "ok   $1 drew $distinct shades"
}

# no rom on the command line: the built-in game
"$BIN" --headless --frames 30 --dump "$PPM" >"$OUT" 2>/dev/null
if ! grep -q "catch the diamond" "$OUT"; then
	echo "FAIL: built-in rom produced no serial output"
	cat "$OUT"
	exit 1
fi
echo "ok   built-in rom booted and printed to the serial port"
check_frame "built-in"

# the same rom, loaded from a file
"$BIN" --headless --frames 30 --dump "$PPM" tests/catch.gb >"$OUT" 2>/dev/null
grep -q "catch the diamond" "$OUT" || { echo "FAIL: file rom produced no serial output"; exit 1; }
echo "ok   rom loaded from a file"
check_frame "file rom"

echo "all tests passed"
