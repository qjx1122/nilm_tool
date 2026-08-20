# NILM_Tool - 三相电压电流波形混合智能压缩

> NILM (Non-Intrusive Load Monitoring) 工具箱中的波形压缩与复原模块，针对电网现场采集数据的高压缩比存储与传输场景。

## 功能特性
- **可配置采样率**：默认 6.4 kHz，支持任意采样率（每周期点数 = sampleRate / 50）
- **高压缩比**：单周期 60-100:1，多周期帧间差分 384:1 (10周期) / 548:1 (50周期)（稳态周期信号）
- **高保真复原**：相似度 >99.6%（余弦相似度），支持 RMSE / SNR 评估
- **混合压缩技术**：
  - FFT 频域压缩 + 自适应谐波选择
  - 谐波字典编码（6 种预定义模式，择优使用，含基波幅相）
  - 帧间差分 + 高效重复帧标记 0xFF（1字节/通道 表示重复前一周期）
  - 零序优化（Clarke 变换 αβ0 域压缩，零序能量<0.001时 1字节表示）
- **三模式**：
  - ULTRA (0): 极致压缩，目标相似度 95%，实测单周期 102:1 / 99.85%
  - BALANCED (1): 平衡，目标 97%，实测 93:1 / 99.69%（含字典优化）
  - HIGH_QUALITY (2): 高质量，目标 99%，实测 93:1 / 99.66%

## 安装 / 环境
- 依赖：GCC (≥11) + libm，无第三方库
- 系统：Linux / macOS / Windows + MinGW
```bash
gcc --version
# Debian 示例
sudo apt-get install build-essential
```

## 运行命令
```bash
# 编译
gcc compress/compress_data.c -o compress_data -lm -O2 -Wall

# 运行基准测试（内置三相含谐波测试信号，128点/周期 + 多周期高压缩比演示）
./compress_data

# 或使用 setup.sh
./setup.sh

# 或 Makefile（如已创建）
make run
```

**v0.3 实测示例输出（节选）**：
```
  压缩模式: 极致压缩
  原始大小: 6144 字节  压缩后大小: 60 字节  压缩比: 102.40:1  平均相似度: 99.85% ✅

  压缩模式: 平衡模式
  原始大小: 6144 字节  压缩后大小: 66 字节  压缩比: 93.09:1   平均相似度: 99.69% ✅

  多周期帧间差分测试 (10 周期)
  总原始大小: 61440 字节  总压缩大小: 160 字节  平均压缩比: 384.00:1 ✅

  多周期帧间差分测试 (50 周期)
  总原始大小: 307200 字节  总压缩大小: 560 字节  平均压缩比: 548.57:1 ✅
```

## 数据目录结构
- **当前版本**：无外部数据依赖，测试信号由 `generate_test_signal()` 生成（220V 三相正弦 + 3次/5次谐波，电流 50A + 3次谐波，50Hz）
- **多周期测试**：内部生成 10/50 周期相同波形，触发重复帧标记，验证高压缩比
- **推荐扩展**：
  - `data/raw/`：现场采集 CSV / COMTRADE
  - `data/compressed/`：压缩后二进制
  - `data/reconstructed/`：复原波形 CSV
  - 大文件勿入 Git，遵循 .gitignore

## 配置文件结构
```c
typedef struct {
    int sampleRate;                // Hz, 默认6400
    int pointsPerCycle;            // = sampleRate/50
    double targetSimilarity;       // 目标相似度
    int minHarmonics;              // 最少谐波
    int maxHarmonics;              // 最多谐波
    double deadZoneThreshold;      // 死区阈值
    int quantBitsFundamental;      // 基波量化位数
    int quantBitsHarmonic;         // 谐波量化位数
    bool enableDictEncoding;       // 字典编码
    bool enableFrameDiff;          // 帧间差分+重复帧
    bool enableZeroOptimization;   // 零序优化
    bool enableAdaptiveHarmonics;  // 自适应
} CompressionConfig;
```
工厂：`create_config(sampleRate, mode)` 默认启用 dict、frameDiff、zero、adaptive。

## 输出产物
- **控制台**：每种模式原始/压缩大小、压缩比、压缩率、6通道相似度、平均 RMSE / SNR、通过状态；多周期平均压缩比
- **统计**：totalFrames / dictUsed / diffUsed / avgHarmonics
- **未来**：输出压缩包文件与复原 CSV 对接 Python NILM

## 生产推荐流程
1. 现场采集 6.4kHz 三相电压电流，整理为每周期 128 点 double
2. 配置：BALANCED 默认；带宽受限 ULTRA；谐波分析 HIGH_QUALITY
3. 三相压缩：`compress_three_phase()` 或 `compress_with_zero_optimization()`（三相平衡时推荐，零序自动 1字节）
4. 长期存储：启用 frameDiff，首帧完整，后续重复帧仅 1字节/通道（0xFF），平均比 >300:1
5. 传输：传输 CompressedData 6段二进制 + size，重复帧特殊标记可进一步与 MQTT/CoAP 结合
6. 复原：`decompress_three_phase()` / `decompress_with_zero_optimization()`，接收端保持历史状态以支持重复帧
7. 评估：`evaluate_compression()` / `evaluate_compression_zero()` 计算相似度、RMSE、SNR

## 关键修复与演进
- **v0.1**：初始提交，编译失败
- **v0.2**：修复 FFT 幅度量化（归一化 2*|F|/N）、harmCount 顺序、零序评估，相似度 >99.8%，单周期 60-100:1
- **v0.3**：增加 prevMean/prevScale、高效重复帧 0xFF、重设计字典编码含基波幅相、多周期测试 384:1/548:1，重新启用字典/差分

## 当前本地状态
- 分支：`arena/01a01ce5-nilm-tool`
- 最新提交：修复版 v0.3
- 已知限制：单周期压缩比仍受 header 5字节限制，<150:1，需三相联合编码或二次熵编码进一步提升
- 下一步：共享 scale/mean，三相联合幅相编码，Huffman 二次压缩

## 开发仪式
遵循 `BOOTSTRAP.md`：
- 开局：`git status/log` + `cat STATUS.md` + `gh auth status` + 检查环境
- 决策落盘到 `STATUS.md`
- 收尾：更新 `STATUS.md` + 追加 `session/NILM_AC_session_complete.md` + 更新 `REPORT.md`/`README.md` + 追加 `REPORT_TEST.md`，提交推送
