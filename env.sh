#!/usr/bin/env bash
# Activate the project Python venv and export the Zephyr build vars
#
# Source it rather than execute it: the activation and the exports
# must land in the caller's shell to survive
source "$(dirname "$0")/.venv/bin/activate"
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=/usr
# Import the tflite-micro module by path, bypassing the west group filter;
# resolved relative to this script so the workspace can move freely
export ZEPHYR_EXTRA_MODULES="$(cd "$(dirname "$0")/.." && pwd)/optional/modules/lib/tflite-micro"
