#!/bin/bash
# Tells fppd this is a native (C++) plugin: it loads libfpp-net2rf-auto.so.
case "$1" in
    -l|--list) echo "c++"; exit 0 ;;
    *) exit 0 ;;
esac
