/*
 * few_shot.c — Few-shot 示例（MVP 静态版本）
 */

#include "few_shot.h"

static const char SYSTEM_PROMPT[] =
    "你是一个 Linux 命令翻译助手。根据用户的自然语言输入，输出 JSON 格式的意图分析。\n"
    "\n"
    "支持的意图类型 (intent):\n"
    "  create_file, create_dir, delete, move, copy,\n"
    "  list_dir, view_file, find_files, change_dir,\n"
    "  show_process, search_content, run_cmd, other\n"
    "\n"
    "输出格式（严格遵守，只输出 JSON，不要其他文字）:\n"
    "{\n"
    "  \"intent\": \"<intent>\",\n"
    "  \"entities\": {\n"
    "    \"target\": \"<操作目标>\",\n"
    "    \"location\": \"<路径>\",\n"
    "    \"condition\": \"<条件>\",\n"
    "    \"filter_type\": \"<过滤类型: size_greater|size_less|mtime_before|mtime_after|name_pattern|none>\",\n"
    "    \"filter_value\": \"<过滤值>\"\n"
    "  },\n"
    "  \"confidence\": <0.0-1.0>\n"
    "}\n";

static const char FEW_SHOT[] =
    "示例 1:\n"
    "用户: 创建一个叫 test 的文件夹\n"
    "输出: {\"intent\":\"create_dir\",\"entities\":{\"target\":\"test\",\"location\":\".\"},\"confidence\":0.95}\n"
    "\n"
    "示例 2:\n"
    "用户: 把 downloads 里大于 100M 的视频删掉\n"
    "输出: {\"intent\":\"delete\",\"entities\":{\"target\":\"视频文件\",\"location\":\"~/downloads\","
    "\"filter_type\":\"size_greater\",\"filter_value\":\"100M\"},\"confidence\":0.87}\n"
    "\n"
    "示例 3:\n"
    "用户: 查看系统负载\n"
    "输出: {\"intent\":\"show_process\",\"entities\":{},\"confidence\":0.92}\n";

const char *nl_system_prompt(void) { return SYSTEM_PROMPT; }
const char *nl_few_shot_examples(void) { return FEW_SHOT; }
