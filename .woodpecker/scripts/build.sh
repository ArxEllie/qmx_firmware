#!/bin/sh
# Build NuPhy Halo75 V2 firmware (ANSI + ISO, default + via keymaps).
# Run inside the qmkfm/qmk_cli image from the repo root.
#
# To build more boards, just add "<keyboard>:<keymap>" lines to TARGETS.
set -eu

# Ensure Python deps are present (qmk_cli image may be missing some)
pip install -q -r requirements.txt

TARGETS="
nuphy/halo75_v2/ansi:default
nuphy/halo75_v2/ansi:via
nuphy/halo75_v2/iso:default
nuphy/halo75_v2/iso:via
"

echo ":::: Fetching the submodules required by the build targets"
make git-submodule

mkdir -p dist

for t in $TARGETS; do
    echo ":::: Building $t"
    make -j"$(nproc)" "$t"
done

echo ":::: Collecting firmware artifacts into dist/"
for ext in bin hex uf2; do
    for f in *."$ext"; do
        [ -f "$f" ] && mv "$f" dist/
    done
done

echo ":::: Generating checksums"
( cd dist && sha256sum -- * > SHA256SUMS )

echo ":::: Build output"
ls -la dist
