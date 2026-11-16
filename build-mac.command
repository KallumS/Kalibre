#!/bin/bash
# Builds Kalibre on this Mac (needs Xcode or its Command Line Tools, and CMake).
# Double-click it in the Finder, or run it in Terminal. The app lands in
# build/Kalibre_artefacts/Release/Kalibre.app. No tests are run here.
set -e
cd "$(dirname "$0")"
if ! command -v cmake >/dev/null; then
  echo "CMake is needed: brew install cmake (https://brew.sh), or download it from cmake.org."
  exit 1
fi
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --target Kalibre --config Release -j "$(sysctl -n hw.ncpu)"
mkdir -p build/Kalibre_artefacts/Release/Kalibre.app/Contents/Resources/Scripts
cp Scripts/*.lua build/Kalibre_artefacts/Release/Kalibre.app/Contents/Resources/Scripts/
open build/Kalibre_artefacts/Release
