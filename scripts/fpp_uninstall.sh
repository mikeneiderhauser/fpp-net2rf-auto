#!/bin/bash

# fpp-net2rf-auto uninstall script (safe to run more than once). The Plugin Manager unloads the plugin through fppd
# before it removes these files, and the plugin creates nothing outside its own directory except the files below.
#
# Settings (config/plugin.fpp-net2rf-auto.json), the controller password (plugindata/fpp-net2rf-auto/) and the log
# (logs/plugin-fpp-net2rf-auto.log) are kept, as FPP itself keeps plugin settings: FPP 10.3 and earlier don't tell a
# Reinstall from an uninstall, and "Reinstall All" after an OS upgrade would otherwise wipe them. The README's
# "Uninstalling" section says how to remove them by hand.

exit 0
