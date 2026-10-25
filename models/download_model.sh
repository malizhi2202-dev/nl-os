#!/bin/bash
# NL-OS 模型下载脚本（魔塔社区 / HuggingFace 双源）
SIZE=${1:-small}
SOURCE=${2:-modelscope}  # modelscope | huggingface

MODEL_DIR="${NL_MODEL_DIR:-/usr/share/nl-os}"

# 目录不可写（非 root 装到 /usr/share）时自动退到用户目录：
# 以前这里静默失败，curl 也跟着失败，但脚本仍退出 0 —— 让人以为下载成功。
if ! mkdir -p "$MODEL_DIR" 2>/dev/null || [ ! -w "$MODEL_DIR" ]; then
    FALLBACK="${XDG_DATA_HOME:-$HOME/.local/share}/nl-os"
    echo "⚠️  $MODEL_DIR 不可写，改用 $FALLBACK"
    MODEL_DIR="$FALLBACK"
    mkdir -p "$MODEL_DIR" || { echo "❌ 无法创建 $MODEL_DIR"; exit 1; }
fi

case $SIZE in
  small)
    FILE="$MODEL_DIR/qwen2.5-0.5b-q4_0.gguf"
    MS_URL="https://www.modelscope.cn/models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/master/qwen2.5-0.5b-instruct-q4_0.gguf"
    HF_URL="https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_0.gguf"
    SIZE_EST="~400MB"
    ;;
  medium)
    FILE="$MODEL_DIR/qwen2.5-1.5b-q4_0.gguf"
    MS_URL="https://www.modelscope.cn/models/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/master/qwen2.5-1.5b-instruct-q4_0.gguf"
    HF_URL="https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_0.gguf"
    SIZE_EST="~1GB"
    ;;
  large)
    FILE="$MODEL_DIR/qwen2.5-3b-q4_0.gguf"
    MS_URL="https://www.modelscope.cn/models/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/master/qwen2.5-3b-instruct-q4_0.gguf"
    HF_URL="https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_0.gguf"
    SIZE_EST="~2GB"
    ;;
esac

URL="${MS_URL}"
[ "$SOURCE" = "huggingface" ] && URL="${HF_URL}"

echo "📥 NL-OS 模型下载"
echo "  大小: $SIZE ($SIZE_EST)"
echo "  源: $SOURCE"
echo "  保存: $FILE"
echo ""

# 幂等：已存在且是合法 GGUF 就直接返回（不要再用 -C 续传，已完整的文件会得到 HTTP 416）
valid_gguf() {
    [ -f "$1" ] && [ "$(head -c 4 "$1" 2>/dev/null)" = "GGUF" ] && \
        [ "$(stat -c %s "$1" 2>/dev/null || echo 0)" -ge 1000000 ]
}

if valid_gguf "$FILE"; then
    echo "📦 已存在且校验通过（$(ls -lh "$FILE" | awk '{print $5}')），跳过下载"
    echo "export NL_MODEL_PATH=$FILE"
    exit 0
fi

if ! curl -L --fail --progress-bar "$URL" -o "$FILE.part"; then
    echo "❌ 下载失败，尝试换个源: bash $0 $SIZE huggingface"
    rm -f "$FILE.part"
    exit 1
fi

# 校验：必须是 GGUF 魔数且体积合理，否则 XML/HTML 错误页也会被当成"成功"
if [ "$(head -c 4 "$FILE.part" 2>/dev/null)" != "GGUF" ] || [ "$(stat -c %s "$FILE.part" 2>/dev/null || echo 0)" -lt 1000000 ]; then
    echo "❌ 下载内容不是有效 GGUF（可能是错误页），已丢弃"
    rm -f "$FILE.part"
    exit 1
fi
mv -f "$FILE.part" "$FILE"

echo "✅ 完成: $FILE（$(ls -lh "$FILE" | awk '{print $5}')）"
echo "export NL_MODEL_PATH=$FILE"