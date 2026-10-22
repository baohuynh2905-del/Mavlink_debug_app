#!/usr/bin/env bash
# Check dependencies, build, test, and optionally run the app.
#   scripts/build.sh            build + run the hardware-free tests
#   scripts/build.sh --run      ... then start the GUI
#   scripts/build.sh --no-tests
set -euo pipefail
cd "$(dirname "$0")/.."
RUN=0; TESTS=1
for a in "$@"; do
    case "$a" in
        --run) RUN=1 ;;
        --no-tests) TESTS=0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

missing=()
for m in Qt6Widgets gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 libpcap; do
    pkg-config --exists "$m" || missing+=("$m")
done
for t in cmake g++ git; do command -v "$t" >/dev/null || missing+=("$t"); done
if ((${#missing[@]})); then
    echo "Missing: ${missing[*]}" >&2
    echo "Install: sudo apt install build-essential cmake git pkg-config qt6-base-dev libgl-dev libpcap-dev \\" >&2
    echo "         libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav" >&2
    exit 1
fi
for e in rtspsrc h264parse avdec_h264; do
    gst-inspect-1.0 "$e" >/dev/null 2>&1 || echo "warning: GStreamer element $e missing: live video will not play" >&2
done

[ -f third_party/PayloadSdk/libs/CMakeLists.txt ] || git submodule update --init
cmake -S . -B build
cmake --build build -j"$(nproc)"
if ((TESTS)); then ctest --test-dir build --output-on-failure; fi
if ((RUN)); then exec ./build/mavlink_debug_app; fi
