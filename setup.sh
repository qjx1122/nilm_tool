#!/bin/bash
set -e
echo "=== NILM_Tool Setup ==="
echo "检查编译器..."
gcc --version
echo "检查依赖: 仅需 libm，标准库已包含"
echo "编译..."
gcc compress/compress_data.c -o compress_data -lm -O2 -Wall
echo "编译完成: ./compress_data"
echo "运行测试..."
./compress_data
echo "=== Setup Done ==="
