cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(edge_ai_gesture)

target_sources(app PRIVATE
	src/main.c
	src/imu.c
	src/capture.c
	src/dfu_boot.c
)
