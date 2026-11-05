#!/bin/bash
set -e

# fpp-net2rf-auto install script: build the plugin against this FPP's headers.
BASEDIR=$(dirname $0)
cd $BASEDIR
cd ..
make "SRCDIR=${SRCDIR}"

# No restartFlag: the plugin declares FPP_PLUGIN_SUPPORTS_UNLOAD, so the Plugin Manager has fppd load it as soon
# as this script finishes.
