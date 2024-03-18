cmake_minimum_required(VERSION 3.22)
project(clox C)

add_executable(clox main.c)

target_compile_features(clox PRIVATE c_std_11)

