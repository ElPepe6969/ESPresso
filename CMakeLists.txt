# ESPresso — ESP-IDF project file
cmake_minimum_required(VERSION 3.16)

# ESP-IDF auto-discovers components/ directory
# Symlinks: components/microlink_comp → microlink/components/microlink
#           components/wireguard_lwip → microlink/components/wireguard_lwip

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(espresso)
