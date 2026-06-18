# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bmi270_act_noflash)

target_sources(app PRIVATE 
               src/bmi2.c
               src/bmi270.c
               src/bmi270_context.c
               src/bmi270_init.c
               src/main.c
)
