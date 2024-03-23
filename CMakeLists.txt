cmake_minimum_required(VERSION 3.22)
project(clox C)

option(DEBUG_TRACE_EXECUTION "Enable tracing of executions in the VM" OFF)

add_executable(clox
        main.c
        common.h
        chunk.h
        chunk.c
        memutil.h
        memutil.c
        debug.h
        debug.c
        value.h
        value.c
        vm.c
        vm.h)

target_compile_features(clox PRIVATE c_std_11)
set_target_properties(clox PROPERTIES
        C_STANDARD 11
        C_EXTENSIONS OFF
        C_STANDARD_REQUIRED ON
)
target_compile_options(clox PRIVATE
        "$<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:GNU>>:-Wall;-Wextra;-Werror;-pedantic>"
        "$<$<CONFIG:DEBUG>:-Og;-Wconversion;-Wno-sign-conversion;-Wno-unused-parameter;-Wno-unused-variable>"
)
target_compile_definitions(clox PRIVATE
        "$<$<BOOL:${DEBUG_TRACE_EXECUTION}>:DEBUG_TRACE_EXECUTION>")
