#!/bin/sh
# Host build of the real firmware drawing/logic code. Needs a C++ compiler (g++ or zig c++).
cd "$(dirname "$0")"
G=${GFX:-$HOME/Arduino/libraries/Adafruit_GFX_Library}
CXX=${CXX:-$(command -v g++ || echo "/opt/zig/ziglang/zig c++")}
$CXX -std=gnu++17 -O1 -DARDUINO=10813 -Wall -Wno-unused-variable -Wno-unused-function -Istub -I"$G" -o render render.cpp "$G/Adafruit_GFX.cpp"
