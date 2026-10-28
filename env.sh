#!/usr/bin/env bash
# Activate the project Python venv and export the Zephyr build vars
#
# Source it rather than execute it: the activation and the exports
# must land in the caller's shell to survive
#   $ source env.sh
source "$(dirname "$0")/.venv/bin/activate"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=/usr/bin/arm-none-eabi-
