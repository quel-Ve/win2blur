cmake_minimum_required(VERSION 3.16)
project(frosted_dwm LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MinHook sources (from DWMBlurGlass)
set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/../../DWMBlurGlass-master/DWMBlurGlassExt/minhook")

add_library(frosted_dwm SHARED
    src/dllmain.cpp
    ${MINHOOK_DIR}/buffer.cpp
    ${MINHOOK_DIR}/hook.cpp
    ${MINHOOK_DIR}/trampoline.cpp
    ${MINHOOK_DIR}/hde/hde32.c
    ${MINHOOK_DIR}/hde/hde64.c
)

target_include_directories(frosted_dwm PRIVATE
    ${MINHOOK_DIR}
    ${MINHOOK_DIR}/hde
)

target_link_libraries(frosted_dwm user32 dbghelp)
