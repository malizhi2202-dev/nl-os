cmake_minimum_required(VERSION 3.16)
project(nl-os VERSION 0.1.0 LANGUAGES C)

# C11 标准（ADR-010）
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 编译选项
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic -Werror
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-unused-function)
add_compile_definitions(_GNU_SOURCE)  # Linux POSIX: PATH_MAX, realpath, strdup, strndup

# Debug 模式下额外选项
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    add_compile_options(-g -O0 -fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# Release 模式下优化
if(CMAKE_BUILD_TYPE MATCHES "Release")
    add_compile_options(-O2 -DNDEBUG)
endif()

# 查找系统依赖
find_package(Threads REQUIRED)

# ============================================================
# third_party 依赖（vendor，锁定版本 · ADR-006）
# ============================================================

# cJSON
add_library(cjson STATIC
    third_party/cJSON/cJSON.c
)
target_include_directories(cjson PUBLIC third_party/cJSON)
target_compile_options(cjson PRIVATE -w -fPIC)  # 抑制 vendor 代码警告 + PIC for shared lib

# llama.cpp + ggml（系统安装 · Phase 2 真实推理）
option(BUILD_STUB_MODEL "Build with stub model loader" OFF)
if(NOT BUILD_STUB_MODEL)
    find_library(LLAMA_LIB llama PATHS /usr/local/lib)
    find_library(GGML_LIB ggml PATHS /usr/local/lib)
    if(LLAMA_LIB AND GGML_LIB)
        message(STATUS "Found system llama.cpp: ${LLAMA_LIB}")
        add_library(llama_sys INTERFACE)
        target_include_directories(llama_sys INTERFACE /usr/local/include)
        target_link_libraries(llama_sys INTERFACE ${LLAMA_LIB} ${GGML_LIB} Threads::Threads m)
    else()
        message(WARNING "llama.cpp not found, using stub model")
        set(BUILD_STUB_MODEL ON)
    endif()
endif()

# ============================================================
# libnlc 核心共享库
# ============================================================
add_library(nlc SHARED
    src/libnlc/json_helper.c
    src/libnlc/path_safety.c
    src/libnlc/rule_engine.c
    src/libnlc/template_fill.c
    src/libnlc/model_loader.c
    src/libnlc/intent_extract.c
    src/libnlc/few_shot.c
    src/libnlc/nlc_api.c
)
target_include_directories(nlc PUBLIC src/libnlc)
target_link_libraries(nlc PUBLIC cjson Threads::Threads)
if(NOT BUILD_STUB_MODEL)
    target_link_libraries(nlc PUBLIC llama_sys)
    target_compile_definitions(nlc PRIVATE GGML_USE_CPU)
endif()

# ============================================================
# nl-perm 权限守护进程
# ============================================================
add_executable(nl-perm
    src/nl-perm/main.c
    src/nl-perm/perm_engine.c
    src/nl-perm/ipc_server.c
    src/nl-perm/audit_log.c
    src/nl-perm/seccomp_filter.c
)
target_link_libraries(nl-perm PRIVATE nlc)

# ============================================================
# nlsh NL-Shell REPL
# ============================================================
add_executable(nlsh
    src/nlsh/main.c
    src/nlsh/dispatch.c
    src/nlsh/history.c
    src/nlsh/line_edit.c
    src/nlsh/ipc_client.c
)
target_link_libraries(nlsh PRIVATE nlc)

# ============================================================
# nl-run .tools 执行器
# ============================================================
add_executable(nl-run
    src/nl-run/main.c
    src/nl-run/script_parser.c
)
target_link_libraries(nl-run PRIVATE nlc)

# ============================================================
# 安装规则
# ============================================================
install(TARGETS nlc nlsh nl-perm nl-run
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)
install(FILES etc/nlsh.conf DESTINATION /etc/nl-os/)
install(FILES etc/perm_rules.conf DESTINATION /etc/nl-os/)
install(FILES etc/nl-perm.service DESTINATION /usr/lib/systemd/system/)

# ============================================================
# 测试（ADR-009）
# ============================================================
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
