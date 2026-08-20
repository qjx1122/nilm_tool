# STATUS.md
## 当前目标
- 实现数据目录结构：`data/` 现场 CSV/COMTRADE 输入，`out/compressed/` 二进制压缩包，`out/reconstructed/` 复原 CSV
- 支持真实波形文件 `data/wave1.csv` (100233行, 10kHz, 约10秒) 的批量压缩与复原，验证压缩比~100:1 相似度>99.9%

## 已完成
- [x] 开局仪式：拉取最新代码 (远程 8fa463d 已包含 data/wave1.csv 7.2M)，当前分支 arena/01a01ce5-nilm-tool，HEAD 8fa463d
- [x] 扩展压缩器支持任意采样率：MAX_POINTS_PER_CYCLE 1024，FFT 支持非2幂 (朴素DFT O(N^2) fallback)，10kHz下 pointsPerCycle=200 可正常压缩
- [x] 重构代码支持库模式：compress_data.c 添加 #ifndef COMPRESSOR_LIB 守卫，允许 file_tool.c 以 #include 方式复用
- [x] 实现文件工具 file_tool.c (编译为 nilm_tool)：
  - ensure_dir 自动创建目录
  - estimate_sample_rate_from_csv 从时间戳差估算采样率 (10kHz)
  - read_csv_wave 动态解析 header (timestamp,UA,IA,UB,IB,UC,IC) 支持任意列顺序
  - 二进制格式：magic "NILM" + version + sampleRate + pointsPerCycle + numFrames + flags + 每帧6通道 size+data (支持0xFF重复标记)
  - write_compressed_bin / write_reconstructed_csv，压缩与解压使用独立历史状态
  - process_data_directory 批量处理 data/*.csv，输出到 out/compressed/*.bin 和 out/reconstructed/*_reconstructed.csv
  - CLI: --mode 0|1|2, --zero-opt 0|1, --input <file>
- [x] Makefile 更新：同时构建 compress_data 和 nilm_tool，增加 dirs/targets
- [x] 实测验证：
  - data/wave1.csv 100233行 10000Hz 200点/周期 501周期
  - ULTRA 45204B 106.43:1 99.99% | BALANCED 48606B 98.98:1 99.99% | HIGH 56829B 84.66:1 99.87%
  - 生成 out/compressed/wave1.bin 48-56KB, out/reconstructed/wave1_reconstructed.csv 7.8MB
- [x] 更新 .gitignore 忽略 out/ 和二进制

## 进行中
- 更新 README.md / REPORT.md / REPORT_TEST.md 文档以反映新目录结构
- 准备 COMTRADE 支持占位 (当前仅 CSV，COMTRADE .cfg/.dat 解析待实现)

## 下一步（TODO）
1. 实现 COMTRADE 标准解析：读取 .cfg 获取通道配置，.dat 读取采样值，映射到 6 通道
2. 进一步优化 Header 共享与三相联合编码，目标单周期 >200:1
3. 二次熵编码：对 .bin 再用 LZ4/Huffman
4. Python 绑定：提供 py 接口直接读写 data/ 和 out/
5. 增加评估脚本：对比 out/reconstructed/ 与 data/ 的 RMSE/SNR 批量报告

## 决策记录 / 踩坑
- **决策：MAX_POINTS 1024**：原 128 无法支持 10kHz (200点/周期)，扩展至 1024 并支持任意点数
- **踩坑：FFT 要求2的幂**：200非2幂，原 Cooley-Tukey 会错，需朴素DFT fallback。已添加 is_power_of_two 判断，非2幂用 O(N^2) DFT
- **决策：库模式守卫**：原 compress_data.c 主函数与新工具冲突，添加 #ifndef COMPRESSOR_LIB 守卫，file_tool.c 定义该宏后 include 实现复用，避免代码重复
- **决策：二进制格式**：采用简单 Header + 每帧6个size+data，便于流式处理，兼容重复标记 0xFF (size=1)
- **决策：时间戳保留**：重构CSV保留原始 timestamp 字符串，确保时间轴一致，便于后续 NILM 对齐
- **踩坑：CSV解析**：header 含空列（末尾逗号），需容错 nfield<=idx_ic 跳过；列顺序可能变化，动态查找索引，缺失时回退默认顺序
- **决策：采样率估算**：从相邻两行秒差 1/diff 估算，异常时回退 10000Hz，确保 10kHz 文件正确识别
- **决策：目录结构**：按用户要求 data/ 输入，out/compressed/ 二进制，out/reconstructed/ CSV，Makefile 增加 dirs 目标

## 关键文件路径
- 核心算法：`compress/compress_data.c` (v0.3 扩展至 1024点, 支持任意点数, 库模式守卫)
- 文件工具：`file_tool.c` -> 编译为 `nilm_tool`，处理 data/ -> out/
- 数据：`data/wave1.csv` (100233行, 7.2M, 10kHz 三相电压电流)
- 压缩输出：`out/compressed/*.bin` (二进制格式 NILM)
- 复原输出：`out/reconstructed/*_reconstructed.csv` (同格式 CSV)
- 构建：`Makefile` (all, run, run-tool, test-file, dirs)
- 文档：`README.md`, `REPORT.md`, `REPORT_TEST.md`, `STATUS.md`
- 会话：`session/NILM_AC_session_complete.md`
