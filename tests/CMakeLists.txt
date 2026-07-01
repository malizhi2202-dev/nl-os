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
