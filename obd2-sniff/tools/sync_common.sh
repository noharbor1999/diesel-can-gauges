#!/bin/sh
# Copy the shared headers into both sketches (Arduino can't include files outside a sketch folder).
cd "$(dirname "$0")/../firmware" && cp common/*.h sniffer_node/ && cp common/*.h gateway/ && echo "synced"
