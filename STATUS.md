# STATUS.md
## 当前目标
- 根据代码生成本算法设计详细说明文档 ALGORITHM_DESIGN.md，完成收尾仪式

## 已完成
- [x] 拉取最新代码：70b89d8 以周波为单位的单/多周波算法 + 对比文件 v0.8
- [x] 实现周波为单位的显式API：
  - 单周波：compress_single_cycle, decompress_single_cycle, compress_single_cycle_joint, decompress_single_cycle_joint（独立，不依赖历史，适用于暂态）
  - 多周波：MultiCycleCompressedData, compress_multi_cycle, decompress_multi_cycle, evaluate_multi_cycle, free_multi_cycle_data（保持历史，帧间差分+0xFF重复）
- [x] 三相联合编码：时域位移N/3残差，条件零序<1%且尺度比<1.2，否则回退独立，实测101.54:1 vs 96.16:1 (+5.6%)
- [x] 二次熵编码：entropy.c LZ4 via dlopen + Huffman，条件二次<一次*0.95且>64B，实测联合+LZ4 157:1 (+64% over baseline)
- [x] 文件工具 v0.8：data/*.csv -> out/compressed/ (bin+lz4+huff), out/reconstructed/, out/comparison/*_comparison.csv (原始/复原/误差), out/verification_report_joint_entropy.csv/txt
- [x] 批量验证4文件：单独立96.16:1 99.55% -> 单联合101.54:1 99.21% -> 多联合+LZ4 157:1，合成稳态10周期384:1 50周期548:1
- [x] 生成详细设计文档 ALGORITHM_DESIGN.md (13K) 和 docs/ALGORITHM_DESIGN.md，包含架构、数据结构、9大算法、条件限制、文件格式、验证结果
- [x] 更新 README, REPORT, REPORT_TEST

## 进行中
- 提交推送

## 下一步（TODO）
1. COMTRADE 解析
2. 电流通道相似度优化
3. Python 绑定与可视化

## 决策记录 / 踩坑
- 决策：显式周波API，单周波禁用hasPrev，多周波保持历史，符合用户“以周波为单位”要求
- 决策：详细设计文档按代码生成，涵盖FFT任意点数、量化、字典择优、重复帧、Clarke零序、联合时域位移、熵编码条件、文件格式、验证数据
- 决策：保留 out/ 在 .gitignore，但验证报告和设计文档在根目录跟踪，便于PR展示

## 关键文件路径
- 设计文档：`ALGORITHM_DESIGN.md`, `docs/ALGORITHM_DESIGN.md`
- 核心：`compress/compress_data.c` (单/多周波API, 联合)
- 熵编码：`entropy.c`
- 工具：`file_tool.c` -> `nilm_tool`
- 数据：`data/wave1-4.csv`
- 输出：`out/compressed/`, `out/reconstructed/`, `out/comparison/`, `out/verification_report_joint_entropy.*`
