cmake_minimum_required(VERSION 3.22)

# Picks up firmware/components/link without copying it.
set(EXTRA_COMPONENT_DIRS ../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(link_poc)
