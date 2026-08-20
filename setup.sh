#!/bin/bash
set -e
echo "=== NILM_Tool Setup v0.4 ==="
echo "检查编译器..."
gcc --version
echo "检查依赖: 仅需 libm，标准库已包含"
echo "创建目录..."
mkdir -p data out/compressed out/reconstructed
echo "编译基准测试..."
gcc compress/compress_data.c -o compress_data -lm -O2 -Wall
echo "编译文件工具..."
gcc file_tool.c -o nilm_tool -lm -O2 -Wall
echo "编译完成: ./compress_data ./nilm_tool"
echo "运行基准测试..."
./compress_data | tail -20
if [ -f data/wave1.csv ]; then
  echo "运行文件压缩测试 (data/wave1.csv)..."
  ./nilm_tool --mode 1 --input data/wave1.csv | tail -20
  ls -lh out/compressed/ out/reconstructed/
else
  echo "data/wave1.csv 不存在，跳过文件测试"
fi
echo "=== Setup Done ==="
