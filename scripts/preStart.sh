#!/bin/sh

# Rebuild only when the library is missing (e.g. an SD image cloned from another architecture). Git updates of
# FPP rebuild every plugin with a Makefile anyway.
BASEDIR=$(dirname $0)
cd $BASEDIR
cd ..

if [ ! -f libfpp-net2rf.so ]; then
    echo "fpp-net2rf: libfpp-net2rf.so is missing, rebuilding"
    make "SRCDIR=${SRCDIR}"
fi
