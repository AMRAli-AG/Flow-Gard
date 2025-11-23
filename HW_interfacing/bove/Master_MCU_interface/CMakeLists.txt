cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED)

project(bove_zephyr_master)

target_sources(app PRIVATE src/main.c)