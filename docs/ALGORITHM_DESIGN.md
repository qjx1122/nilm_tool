# NILM 三相电压电流波形混合智能压缩算法详细设计说明

> 版本：v0.8 (2026-08-20)  
> 核心文件：`compress/compress_data.c` (约 1100 行) + `entropy.c` (LZ4/Huffman) + `file_tool.c` (文件化 I/O)  
> 目标：压缩比 单周期 60-100:1，多周期 384:1 (10周期) / 548:1 (50周期) / 真实文件联合+LZ4 157:1，复原相似度 >95% (实测 >99%)

---

## 1. 设计背景与需求

### 1.1 场景
电网现场采集设备以 6.4kHz-10kHz 采样三相电压电流波形，数据量大（10kHz 采样 1 秒 = 10k 点/通道 ×6通道×8B ≈ 480KB，1 小时 ≈ 1.7GB），需高压缩比存储与低带宽传输，同时保证波形复原后相似度 >95% 以满足谐波分析、NILM 负荷辨识等后续分析。

### 1.2 KPI
- **压缩比** = 原始字节 / 压缩字节，原始 = 6×N×sizeof(double)
- **相似度** = 余弦相似度 dot/(|orig|·|recon|)，6 通道平均，目标 >95%，BALANCED 97%，HIGH 99%
- **辅助**：RMSE、SNR(dB)
- **目录**：`data/` 输入 CSV/COMTRADE，`out/compressed/` 二进制，`out/reconstructed/` 复原 CSV，`out/comparison/` 原始vs复原对比，`out/verification_report_*.csv/txt` 报告

---

## 2. 总体架构

```
data/wave1.csv (timestamp,UA,IA,UB,IB,UC,IC)
   ↓ read_csv_wave() 动态解析 header，估算采样率
WaveData { timestamps[], Va,Vb,Vc,Ia,Ib,Ic, n, sampleRate }
   ↓ 按周波切分 ppc = sampleRate/50 (10kHz→200点)
Frames (numFrames = n/ppc) 每个周波 200点×6通道
   ↓ 压缩器 HybridCompressor (保持历史)
   ├─ 单周波独立：compress_single_cycle() 禁用 hasPrev
   ├─ 单周波联合：compress_single_cycle_joint() 以 Va/Ia 为参考，残差编码
   ├─ 多周波独立：compress_three_phase() 启用 frameDiff，重复帧 0xFF
   └─ 多周波联合：compress_three_phase_joint() + 多周波批量 compress_multi_cycle()
   ↓ 一次压缩二进制 (NILM 格式)
   ↓ 二次熵编码：LZ4 (dlopen) / Huffman，无损，不影响相似度
   ↓ out/compressed/*.bin, *.bin.lz4, *.bin.huff
   ↓ 解压：decompress_three_phase() / decompress_three_phase_joint() / decompress_multi_cycle()
   ↓ out/reconstructed/*.csv + out/comparison/*_comparison.csv (原始/复原/误差)
   ↓ 验证：calculate_similarity / RMSE / SNR → out/verification_report_*.csv/txt
```

---

## 3. 关键数据结构

### 3.1 配置
```c
typedef struct {
  int sampleRate; int pointsPerCycle; // 50Hz工频
  double targetSimilarity;
  int minHarmonics, maxHarmonics; // 自适应范围
  double deadZoneThreshold; // 谐波死区
  int quantBitsFundamental, quantBitsHarmonic;
  bool enableDictEncoding, enableFrameDiff, enableZeroOptimization, enableAdaptiveHarmonics;
} CompressionConfig;
```
工厂 `create_config(sampleRate, mode)`：ULTRA(2-5次谐波,12/4b,95%)，BALANCED(3-10次,14/6b,97%)，HIGH(5-15次,16/8b,99%)

### 3.2 复数与压缩包
```c
typedef struct { double real, imag; } Complex;
typedef struct {
  uint8_t *Va,*Vb,*Vc,*Ia,*Ib,*Ic;
  size_t sizeVa,...; size_t totalSize;
} CompressedData;
typedef struct {
  CompressedData* frames; int numFrames; size_t totalSize;
  int pointsPerCycle, sampleRate; bool isJoint, isMulti;
} MultiCycleCompressedData;
```

### 3.3 压缩器主结构
```c
#define MAX_POINTS_PER_CYCLE 1024
typedef struct {
  CompressionConfig config; int N;
  Complex prevFreq[6][MAX_POINTS_PER_CYCLE]; bool hasPrev[6];
  double prevMean[6], prevScale[6]; // 高效重复帧
  int totalFrames, dictUsed, diffUsed; double avgHarmonics;
} HybridCompressor;
```

### 3.4 字典
```c
typedef struct { int harmonicCount; int* orders; Complex* coefficients; char* name; } DictPattern;
static DictPattern g_dictPatterns[] = {
  PureSine, 3rdHarmonic(5%), 5thHarmonic(3%), 3rd+5th, Rectifier(3,5,7), VFD(3,5,7,9,11)
};
```
系数为归一化幅值比（相对于基波，~1.0 体系），匹配后残差再编码。

---

## 4. 核心算法

### 4.1 FFT 频域压缩（支持任意点数）
- **归一化**：`mean = avg(signal)`，`maxVal = max|signal-mean|`，`norm = (signal-mean)/maxVal`
- **FFT**：`is_power_of_two(n)` 判断，2幂用 Cooley-Tukey，**非2幂用朴素DFT O(N²)** 支持 200 点@10kHz
  ```c
  Fk = Σ s[n]·exp(-j2πkn/N)  // invert=false
  s[n] = (1/N) Σ Fk·exp(j2πkn/N) // invert=true
  ```
- **幅值修复**：原始 FFT 幅值 ~N/2（128点约64），直接量化 0..1.5 会截断。修复为归一化幅值 `fundAmpNorm = raw*2/N` (~1.0)，量化范围 0..2.0，解码还原 `raw = norm*N/2`
- **对称重建**：`freq[n-k] = conj(freq[k])`，基波 `freq[n-1]=conj(freq[1])`，保证 IFFT 后实数

### 4.2 自适应谐波选择
`select_harmonics()`：计算 1..N/2 频点能量，累积能量比 `cum/total` 开方估算相似度，达到 `targetSimilarity` 时停止，限制在 `minHarmonics..maxHarmonics`。禁用时直接返回 `maxHarmonics`。

### 4.3 谐波提取
`extract_harmonics()`：基波必保留，`k=2..K` 若 `amp > deadZone*fundAmp` 则保留，输出 `orders[]` 和 `coeffs[]`。

### 4.4 量化
```c
quantize_16(value, min, max, bits): maxQ=(1<<bits)-1, norm=(value-min)/(max-min) clamp 0..1, Q=norm*maxQ
dequantize_16(Q, min, max, bits): norm=Q/maxQ, value=norm*(max-min)+min
quantize_8 0..255 同理
```
- 均值 -1000..1000 16b，尺度 0..1000 16b
- 基波幅值归一化 0..2.0 12-16b，相位 -π..π 同位数
- 谐波比 -1..1 4-8b，残差 -1..1 8b，差分 -2..2 8b

### 4.5 标准编码（单通道）
Header 5B：`meanQ 2B + scaleQ 2B + flags 1B`  
`flags: [dict 1b][diff 1b][adaptive 1b][K 5b]`  
Body：
- 基波幅相 4B
- 谐波数 1B + 每谐波 5B（order 1B + rQ 2B + iQ 2B），rQ,iQ 为 `coeff/fundAmpRaw` 比值

**修复**：原实现先写谐波后写数量，解码先读数量导致错位，现改为先数量后谐波。

### 4.6 字典编码
- `dict_find_best_match()` 计算谐波次数匹配分数，选最佳 `dictIdx`
- 残差 = 原始谐波 - 字典谐波
- 估算尺寸：`dictSize = 5+4+1+1+res*3` vs `standardSize =5+4+1+stdHarm*5`，**择优**：仅当字典更小时使用
- Body：基波幅相 4B + dictIdx 1B + resCount 1B + 每残差 3B（order 1 + realQ 1 + imagQ 1）

**条件**：字典系数为归一化比，需 `*fundAmpRaw` 还原为 FFT 系数，含基波幅相存储。

### 4.7 帧间差分与高效重复帧
- **重复帧**：若 `hasPrev` 且 `diffEnergy=Σ|F-Fprev|² <1e-6` 且 `|mean-meanPrev|<1e-3` 且 `|scale-scalePrev|/scalePrev<1e-3`，返回 1 字节 `REPEAT_MARKER 0xFF`，解压端复用历史频谱+均值+尺度
- **差分编码**（v0.7 优化后重新启用，择优）：
  - 计算 `diffFreq = freq - prevFreq`
  - 提取显著差分谐波 `amp > deadZone*0.02`，量化 -2..2 8位
  - 尺寸 `diffSize =5+1+diffCount*3`，与标准尺寸择优
  - 解压：`freq = prevFreq + diff`

**单周波**：禁用 `hasPrev`，每帧独立，适用于暂态；**多周波**：启用历史，重复帧 1B/通道，合成稳态 10周期 384:1，50周期 548:1，真实变化负载因每周期均变化，重复不触发，单/多比相同。

### 4.8 Clarke 变换零序优化
```c
abc_to_alpha_beta: α=2/3*(Va-0.5Vb-0.5Vc), β=2/3*0.866*(Vb-Vc), 0=1/3*(Va+Vb+Vc)
alpha_beta_to_abc: 逆变换
```
- 转换到 αβ0 域，零序能量 `ΣV0²` 若 `<0.001` 则 1 字节 `0` 表示，否则压缩零序
- 三相平衡时 `Va+Vb+Vc≈0`，零序可省略，节省 2 通道
- **条件**：`enableZeroOptimization` 且零序能量小；对高度平衡信号有效，字典优化后优势减弱，需 αβ 域专用字典

### 4.9 三相联合编码（Joint）
**原理**：以 `Va/Ia` 为参考，`Vb,Vc,Ib,Ic` 时域循环位移残差编码。

- **位移**：`circular_shift(src,dst,n,shift)`，`shift=N/3` 对应 120°。`Vb ≈ shift(Va, N/3)`，`Vc ≈ shift(Va, -N/3)`，电流同理
- **条件**：`is_balanced_three_phase()` 检查零序比 `<1%` 且尺度比 `<1.2` 且均值≈0，否则回退独立编码
- **编码**：
  - 参考通道 `Va/Ia` 全压缩（5+4+...）
  - 残差通道：计算期望 `expVb=shift(Va,N/3)`，残差 `resVb=Vb-expVb`，压缩残差（残差能量小，尺寸小）
  - 格式：参考 20B + 残差 4×11B=44B → 64B 总计 vs 独立 6×20=120B，节省 ~46%
- **解码**：先解压 `Va/Ia`，位移得到期望，再解压残差相加
- **效果**：4 文件均平衡，单独立 96:1 → 单联合 **101.5:1** (+5.6%)，`wave4` 87→99 (+13%)，相似度 99.55%→99.21% 微降仍>95%

### 4.10 二次熵编码（Entropy）
`entropy.c` 实现：

- **LZ4**：`dlopen("liblz4.so.1")` 运行时加载，`LZ4_compress_default(src,dst,srcSize,dstCap)`，`LZ4_decompress_safe`
- **Huffman**：频率统计→优先队列建树→生成编码→位流打包，格式 `[1B num][symbol+freq 5B*][origSize 4B][bits 4B][bits]`
- **条件**：仅当二次后 `<一次*0.95` 且原尺寸 `>64B` 时使用，避免小文件膨胀；LZ4 适用于重复模式，Huffman 适用于符号分布不均；均为无损，不影响相似度
- **效果**：对 `*_multi_joint.bin`（23-48KB），LZ4 → 15-30KB，压缩比 101→**157-158:1** (+55%)，Huffman 133-146:1 (+40%)

---

## 5. 以周波为单位的单周波与多周波算法

### 5.1 单周波
```c
compress_single_cycle(): 禁用 hasPrev，每周波独立，调用 compress_three_phase()
decompress_single_cycle(): 同理
compress_single_cycle_joint(): 单周波+联合
```
- 特点：每帧独立，无误差累积，适用于暂态、故障、非平衡
- 压缩比：约 96:1（真实文件）

### 5.2 多周波批量
```c
MultiCycleCompressedData compress_multi_cycle(comp, Va,Vb,Vc,Ia,Ib,Ic, totalN, useJoint)
  numFrames = totalN/ppc, frames = calloc(numFrames)
  for fr in 0..numFrames-1: off=fr*ppc, cd = useJoint?joint:three_phase (保持历史)
  totalSize = Σ(cd.totalSize+24)+20
decompress_multi_cycle() 批量解压
evaluate_multi_cycle() 计算全量相似度
```
- 特点：保持历史，支持重复帧 1B/通道，残差差分，对稳态信号极致压缩
- 合成稳态：10周期 384:1，50周期 548:1；真实变化负载因每周期变化，单/多相同 1.00x

---

## 6. 文件 I/O 与目录结构

### 6.1 输入 `data/*.csv`
- 格式 `timestamp,UA,IA,UB,IB,UC,IC,`，动态解析 header 列索引，容错空列
- 采样率估算：`parse_sec_from_ts()` 提取秒部分差 `diff`，`sr=round(1/diff)`，异常回退 10000Hz
- `WaveData` 保留 `timestamps[]` 字符串，确保时间轴一致

### 6.2 输出
- `out/compressed/`：`*_single.bin`, `*_single_joint.bin`, `*_multi.bin`, `*_multi_joint.bin`, `*.lz4`, `*.huff`
- `out/reconstructed/`：复原 CSV，同输入格式
- `out/comparison/`：**对比文件** `*_comparison.csv`，`timestamp,UA_orig,UA_recon,UA_err,IA_orig,IA_recon,IA_err,...`
- `out/verification_report_joint_entropy.csv/txt`：每个文件单/多、独立/联合、LZ4/Huffman 压缩比相似度、零序比/尺度比、是否使用联合

### 6.3 工具 `file_tool.c → nilm_tool`
```bash
./nilm_tool --mode 1 --zero-opt 1
./nilm_tool --input data/wave1.csv --mode 0
```
批量处理 `data/`，自动创建 `out/` 子目录。

---

## 7. 验证结果

### 7.1 真实文件 4 文件 13.15MB (10kHz, 200点/周期)

| 文件 | 单独立 | 单联合 | 多独立 | 多联合 | 多联合+LZ4 | 多联合+Huffman | 对比文件 |
|---|---|---|---|---|---|---|---|
| wave1 100233行 501帧 | 98.94:1 99.48% | 102.18:1 99.21% | 98.94:1 | 102.18:1 | **157.75:1** | 146.52:1 | 19M |
| wave2 68358行 341帧 | 98.67:1 99.41% | 102.06:1 99.16% | 98.67:1 | 102.06:1 | **158.58:1** | 145.34:1 | 13M |
| wave3 50295行 251帧 | 100.09:1 99.59% | 102.97:1 99.28% | 100.09:1 | 102.97:1 | **157.52:1** | 142.24:1 | 9.1M |
| wave4 68342行 341帧 | 87.76:1 99.72% | 99.12:1 98.58% | 87.76:1 | 99.12:1 | **158.08:1** | 133.64:1 | 13M |
| 汇总 | 96.16:1 99.55% | 101.54:1 99.21% | 96.16:1 | 101.54:1 | ~157:1 | ~141:1 | 208M |

- 联合提升 5.6%，LZ4 再提升 55%，总计 +64% over baseline，相似度保持 >98.5%

### 7.2 合成稳态
- 10周期 384:1，50周期 548:1，多周期对稳态极致压缩

---

## 8. 条件限制总结

| 算法 | 适用条件 | 不适用回退 | 影响 |
|---|---|---|---|
| 三相联合 | 零序<1% 且 尺度比<1.2 且均值≈0，平衡 | 回退独立编码 | 压缩比+5-13%，相似度微降0.3% |
| 帧间差分重复 0xFF | diffEnergy<1e-6 且均值/尺度近似不变 | 回退标准/差分/字典择优 | 稳态合成 1B/通道，真实变化不触发 |
| 字典择优 | 字典尺寸<标准尺寸 | 用标准 | 节省谐波存储 |
| LZ4/Huffman | 二次后<一次*0.95 且 >64B | 不使用二次 | 无损，压缩比+40-55% |
| 零序优化 | 零序能量<0.001 | 压缩零序 | 平衡时节省2通道 |

---

## 9. 未来方向
- COMTRADE 解析 .cfg/.dat
- 三相联合频域旋转精确处理正负零序
- 8位 header 共享 scale/mean，目标单周期>150:1
- 二次熵编码后接 Python NILM 模型
- 电流通道独立量化配置提升相似度至>99.9%

---

## 10. 构建与复现
```bash
make clean && make all  # -lm -ldl
./compress_data          # 合成基准
./nilm_tool --mode 1     # 真实文件批量 + 联合 + 熵 + 对比
ls out/compressed/ out/reconstructed/ out/comparison/ out/*.txt out/*.csv
```

遵循 BOOTSTRAP.md 开局与收尾仪式。
