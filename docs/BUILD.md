# 构建说明

## 依赖

- CMake ≥ 3.16
- GCC ≥ 9 或 Clang ≥ 14
- pthread

## 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## ARM64 交叉编译

```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
make -j$(nproc)
```

## 测试

```bash
cmake .. -DBUILD_TESTING=ON
make -j$(nproc)
ctest
```
