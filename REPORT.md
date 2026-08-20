# REPORT.md — 稳定结论报告（NILM_AC）

## 当前推荐稳定版本
- **v0.4 (2026-08-20)**：文件化 I/O 版本，支持 data/ -> out/compressed/ -> out/reconstructed/，兼容 10kHz 真实波形
  - 核心：`compress/compress_data.c` (扩展至 1024点, 支持任意点数DFT fallback, 库模式守卫)
  - 工具：`file_tool.c` -> `nilm_tool` (批量处理 data/*.csv)
  - 数据：`data/wave1.csv` 100233行 10kHz 10秒三相电压电流
  - 输出：`out/compressed/wave1.bin` 二进制 (NILM magic + header + 帧), `out/reconstructed/wave1_reconstructed.csv`
  - 实测：BALANCED 48606B 98.98:1 99.99% 相似度；ULTRA 45204B 106:1；HIGH 56829B 84:1
  - 构建：`make all` -> compress_data + nilm_tool

- **v0.3 (2026-08-20)**：修复版，单周期 90-100:1，多周期 384:1/548:1，字典+帧间差分+零序

## 算法路线演进
- **v0.0 (88462e1)**：初始混合压缩，未编译
- **v0.1**：修复 printStats，相似度 -0.02% (量化截断 & 顺序错)
- **v0.2**：归一化幅值 2*|F|/N 0..2.0，先写count，相似度99.8%+，60-100:1
- **v0.3**：prevMean/prevScale + 0xFF重复帧 + 字典择优，多周期384:1/548:1
- **v0.4 (当前)**：
  - 支持任意点数：MAX 1024，FFT 非2幂用朴素DFT
  - 库模式：#ifndef COMPRESSOR_LIB 守卫，file_tool.c 以 include 复用
  - 文件工具：自动估算采样率 (时间戳差)，动态解析CSV header，保留timestamp字符串，批量处理 data/
  - 二进制格式：NILM magic + version + sampleRate + ppc + numFrames + flags + 每帧6x(size+data)，兼容0xFF重复
  - 实测真实文件：data/wave1.csv 10kHz 200点/周期 501周期，原始4.8MB，压缩48KB，98.98:1，相似度99.99%

## 重大实验结论
1. **FFT 归一化**：幅值 ~N/2 需归一化 2*|F|/N
2. **顺序**：先数量后谐波
3. **Header 瓶颈**：5B header +4B基波=9B/通道，6通道54B，6144B原始，上限113:1
4. **重复帧突破**：1B标记，10周期384:1，50周期548:1
5. **真实数据验证**：10kHz 200点/周期，非2幂需DFT，压缩比~100:1 相似度99.99%达标，说明算法对真实波形鲁棒
6. **目录结构**：data/ 输入，out/compressed/ 二进制，out/reconstructed/ CSV，符合生产流程

## KPI / 验收口径
- **压缩比**：original 6*N*sizeof(double) / compressed (含size头)，单周期理想 >100:1，多周期 >300:1
- **相似度**：余弦相似度平均，目标 >95%，BALANCED 97%，HIGH 99%
- **文件**：data/wave1.csv 100233行，压缩后 48KB，二进制格式可读，复原CSV保留timestamp

## 工程教训
- **非2幂FFT**：200点非2幂，原Cooley-Tukey失效，需DFT fallback
- **库模式**：代码复用需守卫，避免main冲突
- **CSV鲁棒性**：header空列、列顺序变化需动态索引，采样率从时间戳估算
- **目录结构**：out/ 应在 .gitignore 忽略，data/ 保留示例但大文件需外部存储约定

## 配置与数据约定
- **输入**：`data/*.csv` 格式 `timestamp,UA,IA,UB,IB,UC,IC`，时间戳 `YYYY/MM/DD HH:MM:SS.ssss`，采样率从时间戳差估算，默认10000Hz
- **输出**：`out/compressed/*.bin` 二进制，`out/reconstructed/*_reconstructed.csv` 同输入格式
- **采样**：50Hz工频，ppc=sampleRate/50，支持16-1024
- **量化**：基波12-16b，谐波4-8b，均值/尺度16b

## 下一步
- COMTRADE：解析 .cfg/.dat 映射6通道
- 三相联合编码，8位header，二次熵编码
- Python绑定
