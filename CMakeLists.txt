# SPDX-License-Identifier: MIT

if(CONFIG_INPUT_FT6336_TOUCHPAD)
  zephyr_library()
  zephyr_library_sources(drivers/input/input_ft6336_touchpad.c)
endif()
