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
ctest                      # 20 个测试，约 2s
bash scripts/verify_all.sh # AC-1~AC-10 端到端（含 Release 构建与内存门限）
```

## 构建产物

`nlsh` / `nl-perm` / `nl-run` / **`nlbroker`** / `libnlc.so`

## 平台支持状态

| 平台 | 状态 |
|---|---|
| Linux x86_64 + glibc | ✅ 实测（全部测试与端到端） |
| Linux ARM64 | ⚠️ **未验证**（本机无交叉编译链） |
| musl / OpenWrt | ⚠️ **未验证**（同上） |
