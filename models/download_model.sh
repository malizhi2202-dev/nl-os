#!/bin/bash
# NL-OS 模型下载脚本（魔塔社区 / HuggingFace 双源）
SIZE=${1:-small}
SOURCE=${2:-modelscope}  # modelscope | huggingface

MODEL_DIR="${NL_MODEL_DIR:-/usr/share/nl-os}"
mkdir -p "$MODEL_DIR"

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

if [ -f "$FILE" ]; then
    echo "📦 已有文件 $(ls -lh "$FILE" | awk '{print $5}')，使用 -C 断点续传..."
fi

curl -L -C - --progress-bar "$URL" -o "$FILE"
[ $? -eq 0 ] && echo "✅ 完成: $FILE" && echo "export NL_MODEL_PATH=$FILE" || echo "❌ 失败，尝试换个源: bash $0 $SIZE huggingface"
