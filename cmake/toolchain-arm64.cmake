# ARM64 (aarch64) 交叉编译工具链
# 用法: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake ..
#
# 前提: 安装 aarch64-linux-gnu 交叉编译器
#   Debian/Ubuntu: apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 目标 glibc ≥ 2.28（Debian 10 / Ubuntu 18.04+）
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")

# 交叉编译器
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)

# 查找工具
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ARM NEON 优化（ggml 使用）
add_compile_options(-march=armv8-a+simd)
