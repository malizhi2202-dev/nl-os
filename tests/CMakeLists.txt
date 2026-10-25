# 测试目录 CMake（ADR-009: 五层测试金字塔）
# 所有测试使用 #include 方式直接包含被测源码，CMake 只编译测试文件。

# json_helper 测试（T01）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/json_helper_test.c)
    add_executable(test_json_helper unit/json_helper_test.c)
    target_include_directories(test_json_helper PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_json_helper PRIVATE cjson)
    add_test(NAME json_helper COMMAND test_json_helper)
endif()

# path_safety 测试（T02）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/path_safety_test.c)
    add_executable(test_path_safety unit/path_safety_test.c)
    target_include_directories(test_path_safety PRIVATE ${CMAKE_SOURCE_DIR}/src/libnlc)
    add_test(NAME path_safety COMMAND test_path_safety)
endif()

# model_loader 测试（T03）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/model_loader_test.c)
    add_executable(test_model_loader unit/model_loader_test.c)
    target_include_directories(test_model_loader PRIVATE ${CMAKE_SOURCE_DIR}/src/libnlc)
    add_test(NAME model_loader COMMAND test_model_loader)
endif()

# rule_engine 测试（T04）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/rule_engine_test.c)
    add_executable(test_rule_engine unit/rule_engine_test.c)
    target_include_directories(test_rule_engine PRIVATE ${CMAKE_SOURCE_DIR}/src/libnlc)
    add_test(NAME rule_engine COMMAND test_rule_engine)
endif()

# template_fill 测试（T05）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/template_fill_test.c)
    add_executable(test_template_fill unit/template_fill_test.c)
    target_include_directories(test_template_fill PRIVATE ${CMAKE_SOURCE_DIR}/src/libnlc)
    add_test(NAME template_fill COMMAND test_template_fill)
endif()

# intent_extract 测试（T06 · 依赖 model_loader/few_shot/json_helper → 链接 nlc）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/intent_extract_test.c)
    add_executable(test_intent_extract unit/intent_extract_test.c)
    target_include_directories(test_intent_extract PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_intent_extract PRIVATE nlc cjson)
    add_test(NAME intent_extract COMMAND test_intent_extract)
endif()

# nlc_api 测试（T07）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/nlc_api_test.c)
    add_executable(test_nlc_api unit/nlc_api_test.c)
    target_include_directories(test_nlc_api PRIVATE ${CMAKE_SOURCE_DIR}/src/libnlc)
    target_link_libraries(test_nlc_api PRIVATE nlc)
    add_test(NAME nlc_api COMMAND test_nlc_api)
endif()

# perm_engine 测试（T08）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/perm_engine_test.c)
    add_executable(test_perm_engine unit/perm_engine_test.c)
    target_include_directories(test_perm_engine PRIVATE
        ${CMAKE_SOURCE_DIR}/src/nl-perm ${CMAKE_SOURCE_DIR}/src/libnlc)
    add_test(NAME perm_engine COMMAND test_perm_engine)
endif()

# 规则覆盖率测试（T04）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/component/rule_coverage_test.c)
    add_executable(test_rule_coverage component/rule_coverage_test.c)
    target_include_directories(test_rule_coverage PRIVATE ${CMAKE_SOURCE_DIR}/src/libnlc)
    add_test(NAME rule_coverage COMMAND test_rule_coverage)
endif()

# 权限 IPC 测试（T08）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/component/perm_ipc_test.c)
    add_executable(test_perm_ipc component/perm_ipc_test.c)
    target_include_directories(test_perm_ipc PRIVATE
        ${CMAKE_SOURCE_DIR}/src/nl-perm ${CMAKE_SOURCE_DIR}/src/libnlc)
    add_test(NAME perm_ipc COMMAND test_perm_ipc)
endif()

# schema 验证测试（T06 · 链接 nlc + cjson）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/model/schema_validation_test.c)
    add_executable(test_schema_validation model/schema_validation_test.c)
    target_include_directories(test_schema_validation PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_schema_validation PRIVATE nlc cjson)
    add_test(NAME schema_validation COMMAND test_schema_validation)
endif()

# 能力注册表（T02 · nl-capability-runtime）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/capability_registry_test.c)
    add_executable(test_capability_registry unit/capability_registry_test.c)
    target_include_directories(test_capability_registry PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_capability_registry PRIVATE nlc cjson)
    add_test(NAME capability_registry COMMAND test_capability_registry)
endif()

# 能力扫描（T03）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/capability_scan_test.c)
    add_executable(test_capability_scan unit/capability_scan_test.c)
    target_include_directories(test_capability_scan PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_capability_scan PRIVATE nlc cjson)
    add_test(NAME capability_scan COMMAND test_capability_scan)
endif()

# 能力自检（T04）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/capability_selftest_test.c)
    add_executable(test_capability_selftest unit/capability_selftest_test.c)
    target_include_directories(test_capability_selftest PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_capability_selftest PRIVATE nlc cjson)
    add_test(NAME capability_selftest COMMAND test_capability_selftest)
endif()

# 复杂度预算（T05）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/budget_test.c)
    add_executable(test_budget unit/budget_test.c)
    target_include_directories(test_budget PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_budget PRIVATE nlc cjson)
    add_test(NAME budget COMMAND test_budget)
endif()

# 结构化审计（T06）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/audit_test.c)
    add_executable(test_audit unit/audit_test.c)
    target_include_directories(test_audit PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_audit PRIVATE nlc cjson)
    add_test(NAME audit COMMAND test_audit)
endif()

# CIP 协议编解码（T01 · capability-protocol）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/cip_test.c)
    add_executable(test_cip unit/cip_test.c)
    target_include_directories(test_cip PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_cip PRIVATE nlc cjson)
    add_test(NAME cip COMMAND test_cip)
endif()

# 裁决器（T04 · capability-protocol；include .c 以复用 broker 侧单元）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/verdict_test.c)
    add_executable(test_verdict unit/verdict_test.c)
    target_include_directories(test_verdict PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/src/nlbroker ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_verdict PRIVATE nlc cjson)
    add_test(NAME verdict COMMAND test_verdict)
endif()

# ticket（T05 · capability-protocol；include .c 复用 broker 侧单元）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/ticket_test.c)
    add_executable(test_ticket unit/ticket_test.c)
    target_include_directories(test_ticket PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/src/nlbroker)
    target_link_libraries(test_ticket PRIVATE nlc)
    add_test(NAME ticket COMMAND test_ticket)
endif()

# cli 适配器（T06）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/adapter_cli_test.c)
    add_executable(test_adapter_cli unit/adapter_cli_test.c)
    target_include_directories(test_adapter_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/src/nlbroker ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_adapter_cli PRIVATE nlc cjson)
    add_test(NAME adapter_cli COMMAND test_adapter_cli)
endif()

# dbus 适配器（T09/T10）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/adapter_dbus_test.c)
    add_executable(test_adapter_dbus unit/adapter_dbus_test.c)
    target_include_directories(test_adapter_dbus PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/src/nlbroker ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_adapter_dbus PRIVATE nlc cjson)
    add_test(NAME adapter_dbus COMMAND test_adapter_dbus)
endif()

# MCP 客户端适配器（T11）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/adapter_mcp_test.c)
    add_executable(test_adapter_mcp unit/adapter_mcp_test.c)
    target_include_directories(test_adapter_mcp PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/src/nlbroker ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_compile_definitions(test_adapter_mcp PRIVATE FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(test_adapter_mcp PRIVATE nlc cjson)
    add_test(NAME adapter_mcp COMMAND test_adapter_mcp)
endif()

# MCP 门面（T12）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/facade_mcp_test.c)
    add_executable(test_facade_mcp unit/facade_mcp_test.c)
    target_include_directories(test_facade_mcp PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/src/nlbroker ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_facade_mcp PRIVATE nlc cjson)
    add_test(NAME facade_mcp COMMAND test_facade_mcp)
endif()

# http 适配器（T13）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/adapter_http_test.c)
    add_executable(test_adapter_http unit/adapter_http_test.c)
    target_include_directories(test_adapter_http PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/src/nlbroker)
    target_link_libraries(test_adapter_http PRIVATE nlc)
    add_test(NAME adapter_http COMMAND test_adapter_http)
endif()

# 能力存在性/预算拒绝/快路径（T06+T07 · nl-capability-runtime）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/capability_refuse_test.c)
    add_executable(test_capability_refuse unit/capability_refuse_test.c)
    target_include_directories(test_capability_refuse PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/src/nlsh ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_capability_refuse PRIVATE nlc cjson)
    add_test(NAME capability_refuse COMMAND test_capability_refuse)
endif()

# 决策层模型无关部分（T10-T12 · nl-capability-runtime 段 2）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/decision_test.c)
    add_executable(test_decision unit/decision_test.c)
    target_include_directories(test_decision PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_link_libraries(test_decision PRIVATE nlc cjson)
    add_test(NAME decision COMMAND test_decision)
endif()

# 中文评测集（T13 · nl-capability-runtime 段 2）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/benchmark_test.c)
    add_executable(test_benchmark unit/benchmark_test.c)
    target_include_directories(test_benchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_compile_definitions(test_benchmark PRIVATE
        BENCHMARK_PATH="${CMAKE_SOURCE_DIR}/models/benchmarks/nl-commands-zh-v2.json")
    target_link_libraries(test_benchmark PRIVATE nlc cjson)
    add_test(NAME benchmark COMMAND test_benchmark)
endif()

# 解码后端 + 受约束 Choice（T11b/T13 · nl-capability-runtime 段 2）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/decision_model_test.c)
    add_executable(test_decision_model unit/decision_model_test.c)
    target_include_directories(test_decision_model PRIVATE
        ${CMAKE_SOURCE_DIR}/src/libnlc ${CMAKE_SOURCE_DIR}/third_party/cJSON)
    target_compile_definitions(test_decision_model PRIVATE
        BENCHMARK_PATH="${CMAKE_SOURCE_DIR}/models/benchmarks/nl-commands-zh-v2.json")
    target_link_libraries(test_decision_model PRIVATE nlc cjson)
    add_test(NAME decision_model COMMAND test_decision_model)
endif()

# .tools 固化（T14）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/freeze_test.c)
    add_executable(test_freeze unit/freeze_test.c)
    target_include_directories(test_freeze PRIVATE ${CMAKE_SOURCE_DIR}/src/libnlc)
    target_link_libraries(test_freeze PRIVATE nlc)
    add_test(NAME freeze COMMAND test_freeze)
endif()

# 快照与撤销（T15）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/snapshot_test.c)
    add_executable(test_snapshot unit/snapshot_test.c)
    target_include_directories(test_snapshot PRIVATE ${CMAKE_SOURCE_DIR}/src/libnlc)
    target_link_libraries(test_snapshot PRIVATE nlc)
    add_test(NAME snapshot COMMAND test_snapshot)
endif()
