#!/bin/sh
# Builds the Arkanoid ROM from the annotated disassembly and checks that it
# is byte for byte the original cartridge.
#
#   tools/build_rom.sh [disassembly directory]
#
# The disassembly is not part of this repository:
#   git clone https://github.com/mcolom/arkanoid_msx_disasm reference/arkanoid
#
# The disassembly is written for z80asm 1.8 (https://savannah.nongnu.org/
# projects/z80asm). One line uses the form "ld (ix), 0", which that
# assembler crashes on; it is assembled as "ld (ix+0), 0", which encodes to
# the same four bytes (DD 36 00 00) that the disassembly's own comment gives
# for that line.
set -e

SOURCE=${1:-${ARKANOID_ASM:-reference/arkanoid}}
OUT=${ARKANOID_ROM:-build/arkanoid.rom}
WORK=build/rom

if [ ! -f "$SOURCE/disassembly.asm" ]; then
	echo "no disassembly in $SOURCE" >&2
	echo "git clone https://github.com/mcolom/arkanoid_msx_disasm $SOURCE" >&2
	exit 1
fi

command -v z80asm >/dev/null 2>&1 || {
	echo "z80asm not found; build z80asm 1.8 and put it in PATH" >&2
	exit 1
}

rm -rf "$WORK"
mkdir -p "$WORK" "$(dirname "$OUT")"
cp -r "$SOURCE/." "$WORK/"
sed -i 's/ld (ix), 0/ld (ix+0), 0/' "$WORK/disassembly.asm"

(cd "$WORK" && z80asm disassembly.asm > asm.log 2>&1) || {
	echo "assembly failed:" >&2
	cat "$WORK/asm.log" >&2
	exit 1
}

mv "$WORK/a.bin" "$OUT"

EXPECTED=2183f07fa3ba87360100b2fa21fda0f55c0f8814
ACTUAL=$(sha1sum "$OUT" | cut -d' ' -f1)
if [ "$ACTUAL" != "$EXPECTED" ]; then
	echo "checksum mismatch: $ACTUAL" >&2
	echo "expected:          $EXPECTED" >&2
	exit 1
fi

echo "$OUT: $(wc -c < "$OUT") bytes, sha1 $ACTUAL (the original cartridge)"
