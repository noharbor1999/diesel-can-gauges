#!/bin/sh
# Verify the shared packet header is identical in both sketches.
cd "$(dirname "$0")/.." && cmp hub/g3_packet.h display/g3_packet.h && echo "g3_packet.h: identical"
