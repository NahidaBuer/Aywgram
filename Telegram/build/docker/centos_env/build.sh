#!/bin/bash
set -e

ayu_build_dir="${AYW_BUILD_DIR:-$PWD/out/build/linux-x86_64/${CONFIG:-Release}/default}"
cd Telegram
./configure.sh -B "$ayu_build_dir" "$@"
# KEEP_GOING=1 lets ninja report every failing translation unit in one
# run instead of stopping at the first; the exit status still fails.
cmake --build "$ayu_build_dir" --config "${CONFIG:-Release}" ${KEEP_GOING:+-- -k 0}
