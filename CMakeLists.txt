cmake_minimum_required(VERSION 3.22)
project(clox C)

add_executable(clox
        main.c
        common.h
        chunk.h
        chunk.c
        memory.h
        memory.c
        debug.h
        debug.c
        value.h
        value.c)

target_compile_features(clox PRIVATE c_std_11)
set_target_properties(clox PROPERTIES
        C_STANDARD 11
        C_EXTENSIONS OFF
        C_STANDARD_REQUIRED ON
)
target_compile_options(clox PRIVATE
        -Wall -Wextra -Werror -pedantic
        # Annoying in debug builds when you're just trying to get something working
        "$<$<CONFIG:DEBUG>:-Wno-unused-parameter;-Wno-unused-variable>"
)
