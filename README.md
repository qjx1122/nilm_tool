# NILM_Tool - 三相电压电流波形混合智能压缩

> NILM 工具箱中的波形压缩与复原模块，支持现场 CSV/COMTRADE 批量处理，输出到 `out/` 目录。

## 功能特性
- **可配置采样率**：支持任意采样率（默认 6.4kHz，实测 10kHz@data/wave1.csv），pointsPerCycle = sampleRate/50，最大 1024点，FFT 支持非2幂（DFT fallback）
- **高压缩比**：单周期 60-106:1，10周期 384:1，50周期 548:1；真实文件 data/wave1.csv (100233行 10kHz) 实测 98.98:1 相似度 99.99%
- **高保真复原**：余弦相似度 >99.6%，支持 RMSE / SNR
- **混合压缩技术**：FFT频域 + 自适应谐波 + 字典择优 + 帧间差分重复标记 0xFF + 零序优化
- **文件化 I/O**：`data/` 输入，`out/compressed/` 二进制，`out/reconstructed/` CSV

## 目录结构（用户要求）
```
nilm_tool/
├── data/                         # 现场采集 CSV / COMTRADE
│   └── wave1.csv                 # 示例：100233行 timestamp,UA,IA,UB,IB,UC,IC 10kHz
├── out/
│   ├── compressed/               # 压缩后二进制 *.bin
│   │   └── wave1.bin             # 格式: NILM magic + header + 每帧6通道 size+data
│   └── reconstructed/            # 复原波形 CSV
│       └── wave1_reconstructed.csv # 同输入格式，timestamp,UA,IA,UB,IB,UC,IC
├── compress/
│   └── compress_data.c           # 核心压缩库 (v0.3+，支持1024点，库模式守卫)
├── file_tool.c                   # 文件批处理工具 -> 编译为 nilm_tool
├── Makefile
└── README.md
```

**二进制格式** (`out/compressed/*.bin`)：
```
Header (20B):
  magic 4B "NILM"
  version u32 (1)
  sampleRate i32
  pointsPerCycle i32
  numFrames i32
  flags i32 (bit0=zeroOpt, bit1=dict, bit2=frameDiff)
Per Frame (numFrames 次):
  sizes[6] u32[6]  # Va,Vb,Vc,Ia,Ib,Ic 各通道压缩后大小
  data0 size0 B
  data1 size1 B
  ...
  data5 size5 B
  (size==1 && data[0]==0xFF 表示重复前一帧，极致压缩)
```

## 安装 / 环境
```bash
gcc --version
sudo apt-get install build-essential
```

## 运行命令
```bash
# 编译两个二进制：compress_data (基准测试) 和 nilm_tool (文件工具)
make all

# 基准测试（内置合成信号）
./compress_data

# 文件压缩工具 - 处理 data/ 下所有 CSV
make dirs
./nilm_tool --mode 1 --zero-opt 1
# 或指定单文件
./nilm_tool --input data/wave1.csv --mode 0

# 查看结果
ls -lh out/compressed/ out/reconstructed/
head out/reconstructed/wave1_reconstructed.csv

# Makefile 快捷
make run          # 基准测试
make run-tool     # 文件批处理
make test-file    # 单文件测试
```

**实测输出** (data/wave1.csv 10kHz)：
```
读取 data/wave1.csv: 100233 行, 估算采样率 10000 Hz
使用 pointsPerCycle=200 (sampleRate=10000)
压缩文件已写入 out/compressed/wave1.bin: 原始 4811184 字节, 压缩 48606 字节, 压缩比 98.98:1
复原文件已写入 out/reconstructed/wave1_reconstructed.csv, 平均相似度 99.9918%

模式对比：
  ULTRA (0): 45204B 106.43:1 99.99%
  BALANCED (1): 48606B 98.98:1 99.99%
  HIGH (2): 56829B 84.66:1 99.87%
```

## 配置
```c
create_config(sampleRate, mode) // mode 0=ULTRA 1=BALANCED 2=HIGH_QUALITY
// 默认启用 dict, frameDiff, zeroOpt, adaptive
```

## 生产流程
1. **采集**：现场设备导出 CSV (`timestamp,UA,IA,UB,IB,UC,IC`) 或 COMTRADE (.cfg/.dat) 放入 `data/`
2. **压缩**：`./nilm_tool --mode 1` 批量处理，生成 `out/compressed/*.bin`
3. **传输/存储**：.bin 文件可直接存储或通过 MQTT/CoAP 传输，重复帧仅 1B/通道
4. **复原**：工具自动生成 `out/reconstructed/*.csv`，或调用 `decompress_three_phase()` 在接收端重建
5. **评估**：对比 `data/` 与 `out/reconstructed/` 计算相似度/RMSE/SNR
6. **NILM**：复原 CSV 可直接输入 Python NILM 模型

## COMTRADE 支持
- 当前版本主要支持 CSV，COMTRADE 预留接口
- 计划：解析 .cfg 获取通道配置，.dat 读取采样值，映射到 6 通道
- 临时方案：若有 .cfg/.dat，可先转换为 CSV 格式 `timestamp,UA,IA,...` 再处理

## 演进
- v0.1 编译失败
- v0.2 修复量化与顺序，相似度>99.8% 单周期60-100:1
- v0.3 重复帧 0xFF 多周期384:1/548:1
- v0.4 (当前) 支持 data/ out/ 目录结构，10kHz真实波形，1024点，任意点DFT，文件工具 nilm_tool，二进制格式，提供 batch 处理

## 开发仪式
遵循 BOOTSTRAP.md 开局与收尾。
