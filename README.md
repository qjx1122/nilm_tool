# NILM_Tool - 三相电压电流波形混合智能压缩 (v0.8)

> 支持 `data/` 现场 CSV/COMTRADE 批量处理，`out/` 输出压缩、复原、对比与报告，集成三相联合编码与二次熵编码。

## 功能特性
- **任意采样率**：支持 6.4kHz-50kHz，`ppc=sampleRate/50`，MAX 1024点，FFT 支持非2幂 DFT fallback，实测 10kHz@data/wave1-4.csv
- **高压缩比**：基线独立 ~96:1，**三相联合 101.5:1 (+5.6%)**，**联合+LZ4 157-158:1 (+64% over baseline)**，联合+Huffman 133-146:1
- **高保真**：相似度 >98.5% (联合后)，独立 >99.4%，满足 >95% 目标
- **混合技术**：FFT频域 + 自适应谐波 + 字典择优 + 帧间差分重复 0xFF + 零序Clarke + **三相联合时域位移残差** + **二次熵编码 LZ4/Huffman**
- **文件化 I/O**：`data/` 输入，`out/compressed/` 二进制，`out/reconstructed/` 复原，`out/comparison/` 原始vs复原对比

## 目录结构（用户要求）
```
nilm_tool/
├── data/                         # 现场采集 CSV / COMTRADE (4文件示例)
│   ├── wave1.csv (100233行 7.2M)
│   ├── wave2.csv (68358行 4.9M)
│   ├── wave3.csv (50295行 3.6M)
│   └── wave4.csv (68342行 4.9M)
├── out/
│   ├── compressed/               # 一次压缩 *.bin + 二次熵 *.lz4 *.huff
│   │   ├── wave1_single.bin (独立)
│   │   ├── wave1_single_joint.bin (联合)
│   │   ├── wave1_multi_joint.bin (多周期联合)
│   │   ├── wave1_multi_joint.bin.lz4 (二次 LZ4)
│   │   └── wave1_multi_joint.bin.huff (二次 Huffman)
│   ├── reconstructed/            # 复原波形 CSV 同输入格式
│   ├── comparison/               # 新增：原始与复原对比 CSV
│   │   └── wave1_single_joint_comparison.csv
│   │       # timestamp,UA_orig,UA_recon,UA_err,IA_orig,IA_recon,IA_err,...
│   └── verification_report_joint_entropy.csv/txt
│       # 汇总每个文件单/多、独立/联合、LZ4/Huffman压缩比相似度
├── compress/
│   └── compress_data.c           # 核心库 v0.8 (1024点, DFT, 联合, 库守卫)
├── entropy.c                     # 二次熵编码：LZ4 via dlopen + Huffman
├── file_tool.c                   # 文件批处理工具 -> nilm_tool
├── Makefile (-lm -ldl)
└── README.md
```

**二进制格式** (`*.bin`)：
```
Header 20B: "NILM" + version(u32) + sampleRate(i32) + ppc(i32) + numFrames(i32) + flags(i32 bit0=zero,1=dict,2=frameDiff,3=joint)
PerFrame: sizes[6] u32[6] + data[6]
  size==1 && data[0]==0xFF 重复帧
```

**对比文件** (`*_comparison.csv`)：
```
timestamp,UA_orig,UA_recon,UA_err,IA_orig,IA_recon,IA_err,UB_orig,UB_recon,UB_err,IB_orig,IB_recon,IB_err,UC_orig,UC_recon,UC_err,IC_orig,IC_recon,IC_err
2020/09/21 10:03:18.9996,39.33,46.41,-7.07,-0.68,-0.50,-0.17,...
```

## 安装与运行
```bash
make all && make dirs
./compress_data                     # 合成信号基准：单周期102:1 多周期384:1/548:1
./nilm_tool --mode 1                # 批量处理 data/*.csv -> out/ (单/联合/多/熵 + 对比)
./nilm_tool --input data/wave1.csv --mode 0  # 单文件

ls -lh out/compressed/ out/reconstructed/ out/comparison/
cat out/verification_report_joint_entropy.txt
head out/comparison/wave1_single_joint_comparison.csv
```

**实测 v0.8**（BALANCED，4 文件）：
```
文件: wave1 (100233行 10kHz 200ppc 501帧)
  平衡检查: 零序比0.15% 尺度比1.18 [适合联合]
  单独立 98.94:1 99.48%  单联合 102.18:1 99.21% [联合更优] +3.3%
  多独立 98.94:1 99.48%  多联合 102.18:1 99.21%
  多联合+LZ4 157.75:1 (+59% over baseline)  Huffman 146.52:1
  对比文件 out/comparison/wave1_*_comparison.csv (原始/复原/误差)

汇总 4文件 总原始13.15MB
单独立96.16:1 单联合101.54:1 (+5.6%) 多独立96.16:1 多联合101.54:1
多联合+LZ4平均 ~157-158:1 (+64% over baseline) 相似度保持>98.5%
```

## 两种增强算法的使用条件限制

### 三相联合编码
- **适用**：三相平衡或近似平衡，零序能量比 `<1%` 且 尺度比 `max/min <1.2` 且均值接近0。此时 Vb,Vc 可由 Va 时域位移 N/3 得到，残差小，压缩比提升 3-13%（wave4 13%）。
- **不适用**：三相不平衡、大量零序、暂态、单相接地故障等，会回退独立编码以保证相似度。
- **实现**：`is_balanced_three_phase()` 检查，`compress_three_phase_joint()` 若不平衡则 `return compress_three_phase()`。

### 二次熵编码（LZ4/Huffman）
- **LZ4**：通过 `dlopen` 运行时加载 `liblz4.so.1`，适用于含重复模式、字典可压缩数据。对已高度量化高熵数据提升有限，小文件（<64B）头部开销可能膨胀。
  - 条件：仅当二次后 `< 一次*0.95` 且原尺寸 `>64B` 时使用
- **Huffman**：纯C实现，统计频率建树，适用于符号分布不均匀数据，需存储频率表，表头开销 `1+present*5+8`，对小文件不划算，条件同上。
- 两者均为**无损**二次压缩，不影响相似度，只提升压缩比。

## 配置
`create_config(sampleRate, mode)` mode 0=ULTRA 1=BALANCED 2=HIGH

## 演进
- v0.4 data/out 目录
- v0.5 批量验证4文件
- v0.6 单/多周期对比
- v0.7 联合+熵编码 101:1 → 157:1
- v0.8 **对比文件** out/comparison/ 原始vs复原

遵循 BOOTSTRAP.md 仪式。
