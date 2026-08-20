# STATUS.md
## 当前目标
- 使用 data/ 下所有文件运行验证测试，统计每个文件的压缩比和相似度指标，生成 out/verification_report.csv/txt

## 已完成
- [x] 拉取最新代码：远程 f7771fa 包含 4 个波形文件 wave1-4.csv (共 21M, 287232行)
- [x] 增强 file_tool.c 实现详细验证：
  - 新增 FileMetrics 结构体：filename, rows, sampleRate, ppc, numFrames, originalBytes, compressedBytes, ratio, avgSimilarity, per-channel sim/RMSE/SNR
  - verify_and_compress 函数：压缩并解压计算全量指标，写入 out/compressed/*.bin 和 out/reconstructed/*.csv
  - process_data_directory 批量处理并生成汇总报告 out/verification_report.csv/txt，含汇总平均压缩比和相似度
- [x] 批量验证结果 (BALANCED 模式, 零序优化启用)：
  - wave1.csv 100233行 10kHz 200ppc 501帧 原始4.8M 压缩48KB 98.94:1 相似度99.48% (Va 99.99% Vb 99.99% Vc 99.99% Ia 99.08% Ib 99.06% Ic 98.77%)
  - wave2.csv 68358行 10kHz 200ppc 341帧 原始3.2M 压缩33KB 98.67:1 相似度99.41% (Va 99.99% Vb 99.99% Vc 99.99% Ia 99.11% Ib 98.65% Ic 98.75%)
  - wave3.csv 50295行 10kHz 200ppc 251帧 原始2.4M 压缩24KB 100.09:1 相似度99.59% (Va 99.99% Vb 99.99% Vc 99.99% Ia 99.27% Ib 99.21% Ic 99.07%)
  - wave4.csv 68342行 10kHz 200ppc 341帧 原始3.2M 压缩37KB 87.76:1 相似度99.72% (Va 99.83% Vb 99.84% Vc 99.82% Ia 99.59% Ib 99.82% Ic 99.42%)
  - 汇总：4文件 总原始13.15MB 总压缩0.14MB 平均压缩比96.16:1 平均相似度99.55%
- [x] 生成报告：out/verification_report.csv (机器可读) 和 out/verification_report.txt (人类可读)
- [x] Makefile 已支持 make run-tool / test-file

## 进行中
- 更新 REPORT_TEST.md 追加批量验证专题，更新 README 和 REPORT

## 下一步（TODO）
1. 实现 COMTRADE 解析
2. 优化电流通道相似度 (当前 ~98-99%，电压已>99.9%)，可通过提高电流谐波量化位数或单独配置
3. 二次熵编码提升压缩比至 >150:1
4. Python 绑定与可视化

## 决策记录 / 踩坑
- **决策：详细指标统计**：新增 per-channel RMSE/SNR 和相似度，便于定位电流通道相似度略低于电压通道的问题 (电压 ~99.99%，电流 ~98.7-99.5%)
- **踩坑：wave4 压缩比偏低 87:1**：其波形可能含更多暂态或谐波畸变，导致字典无法高效匹配，谐波数增多，压缩包增大。相似度仍 99.72% 达标，说明算法对复杂波形鲁棒但压缩比下降
- **决策：汇总报告**：生成 CSV + TXT 双格式，CSV 便于 Python/Excel 分析，TXT 便于人类阅读
- **决策：保持 BALANCED 模式**：平衡模式下平均 96:1 已接近超高压缩，ULTRA 可达 106:1 但电流相似度可能进一步下降，HIGH 84:1 更保真

## 关键文件路径
- 核心：`compress/compress_data.c` (1024点, DFT fallback, 库守卫)
- 工具：`file_tool.c` -> `nilm_tool` (批量验证 + 报告)
- 数据：`data/wave1-4.csv` (4文件 21M)
- 输出：`out/compressed/*.bin` (148KB 总计), `out/reconstructed/*.csv` (23M), `out/verification_report.csv/txt`
- 报告：`REPORT_TEST.md` 新增验证专题
