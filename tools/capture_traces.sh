#!/bin/sh
# Captures the interpreter's behaviour for every sound code, as a file to work
# against. Run after make: tools/capture_traces.sh build/arkanoid.rom > out.txt
set -e
ROM=${1:-build/arkanoid.rom}
TICKS=${2:-30}
code=0
while [ "$code" -lt 256 ]; do
	./isr_multi "$ROM" "$code" "$TICKS"
	code=$((code + 1))
done
