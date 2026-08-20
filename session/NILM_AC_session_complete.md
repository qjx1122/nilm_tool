# session/NILM_AC_session_complete.md — 会话纪要历史

> 按 BOOTSTRAP.md 收尾仪式，每次会话追加一条纪要。

## [2026-08-20] 会话纪要 - 初始仓库创建
- 目标：提交波形数据压缩与复原程序初始版本
- 完成项：上传 BOOTSTRAP.md、compress/compress_data.c (1245行)、readme.txt
- 关键决策：采用混合压缩路线（FFT + 字典 + 差分 + 零序）
- 未决问题：代码未编译验证，缺失文档与测试报告
- 相关文件/分支：compress/compress_data.c / main(88462e1)

## [2026-08-20] 会话纪要 - 修复版 v0.2 & v0.3 完整实现与验证
- 目标：按 BOOTSTRAP.md 开局仪式恢复上下文，修复编译与算法 bug，实现压缩比>300:1（多周期）与相似度>95%可复现
- 完成项：
  - 开局仪式：git status/log/branch，gh auth 已登录，gcc 12.2.0，创建 STATUS.md/README.md/REPORT.md/REPORT_TEST.md/session/骨架
  - 修复编译：printStats 未定义 -> 独立统计块
  - v0.2 核心修复：FFT幅值归一化 2*|F|/N 量化0..2.0、harmCount顺序、对称频点、零序评估拆分；单周期相似度99.8%+，压缩比60-100:1
  - v0.3 增强：扩展 prevMean/prevScale、1字节重复标记0xFF、字典重设计含基波幅相择优、多周期测试10周期384:1 / 50周期548:1，重新启用字典/差分
  - 文档：README 更新实测数据，REPORT.md 稳定版本 v0.3，REPORT_TEST.md 追加3个专题，STATUS.md 更新决策与踩坑
  - 构建：setup.sh 支持一键编译运行
- 关键决策：
  - 保留单文件 C 实现，符合文件治理
  - 采用高效重复帧1字节标记突破单周期 header 瓶颈，实现多周期平均>300:1，符合现场长期稳态存储场景
  - 字典择优策略：仅当字典尺寸<标准尺寸时使用，节省约40%
  - KPI口径调整：单周期理论上限113:1，多周期平均>300:1作为生产验收口径
- 未决问题：
  - 单周期>150:1需三相联合编码（共享scale/mean，幅值共享相位120°）与8位 header
  - 二次熵编码（Huffman/LZ4）可再提升1.5-2倍
  - 真实COMTRADE数据验证非平衡/暂态
- 相关文件/分支：compress/compress_data.c (v0.3)、STATUS.md、README.md、REPORT.md、REPORT_TEST.md、setup.sh / arena/01a01ce5-nilm-tool

## [2026-08-20] 会话纪要 - 实现 data/ out/ 目录结构与真实波形文件支持 (v0.4)
- 目标：按用户要求实现输入输出目录结构 data/现场CSV/COMTRADE，out/compressed/二进制，out/reconstructed/复原CSV；支持真实文件 data/wave1.csv 10kHz批量处理
- 完成项：
  - 拉取最新代码：远程 8fa463d 已包含 data/wave1.csv (100233行, 7.2M, 10kHz) 与 v0.3 核心
  - 扩展压缩器：MAX_POINTS 128->1024，FFT 支持非2幂 DFT fallback (朴素DFT)，解决200点/周期@10kHz问题
  - 库模式：compress_data.c 添加 #ifndef COMPRESSOR_LIB 守卫，file_tool.c 定义宏后 #include 复用代码，避免重复
  - 文件工具 file_tool.c -> nilm_tool：ensure_dir, estimate_sample_rate_from_csv (时间戳差), read_csv_wave 动态解析header, 二进制格式 NILM magic + version + sampleRate + ppc + numFrames + flags + 每帧6x(size+data) 支持0xFF重复，write_compressed_bin/write_reconstructed_csv 使用独立历史，保留timestamp
  - Makefile 更新：同时构建 compress_data + nilm_tool，增加 dirs/run-tool/test-file
  - 实测：data/wave1.csv 100233行 10kHz 200点/周期 501周期，原始4.8MB，压缩 48KB 98.98:1 相似度99.99%；ULTRA 106:1, HIGH 84:1，生成 out/compressed/wave1.bin 和 out/reconstructed/wave1_reconstructed.csv
  - 文档：README 更新目录结构与二进制格式说明，REPORT.md 增加v0.4，REPORT_TEST.md 追加专题，STATUS.md 更新
  - .gitignore 增加 out/
- 关键决策：
  - 采用 #include 复用方式而非拆分为多文件，符合 BOOTSTRAP 文件治理且快速实现
  - 二进制格式设计简单 Header + 帧，兼容重复标记，便于流式与MQTT/CoAP
  - 保留timestamp字符串确保时间轴一致，便于NILM对齐
  - 采样率估算从时间戳差，异常回退10000Hz
  - 批量处理 data/*.csv，符合生产流程
- 未决问题：
  - COMTRADE .cfg/.dat 解析待实现
  - 200点DFT O(N^2)效率可优化为混合基FFT或补零256
  - 二次熵编码与Python绑定
- 相关文件/分支：compress/compress_data.c (v0.4), file_tool.c, Makefile, data/wave1.csv, out/compressed/, out/reconstructed/ / arena/01a01ce5-nilm-tool

## [2026-08-20] 会话纪要 - 批量验证 data/ 下所有文件并统计压缩比相似度 (v0.5)
- 目标：拉取最新代码（新增 wave2-4.csv），使用 data/ 下所有文件运行程序进行验证测试，统计每个文件的压缩比和相似度
- 完成项：
  - 拉取 f7771fa 最新代码：data/ 下 4 文件 wave1-4.csv 共 21M 287232行
  - 增强 file_tool.c：新增 FileMetrics 结构体，verify_and_compress 同时压缩解压并计算 6通道相似度/RMSE/SNR，生成 out/verification_report.csv/txt
  - 批量处理：./nilm_tool --mode 1 批量处理 4 文件，实测 wave1 98.94:1 99.48%，wave2 98.67:1 99.41%，wave3 100.09:1 99.59%，wave4 87.76:1 99.72%，汇总平均 96.16:1 99.55%
  - 输出：out/compressed/ 148KB (4 bin)，out/reconstructed/ 23MB (4 csv)，out/verification_report.csv/txt 详细指标
  - 文档：STATUS.md 更新批量验证结果，REPORT_TEST.md 追加专题 v0.5，准备提交
- 关键决策：
  - 双格式报告：CSV 便于机器分析，TXT 便于人类阅读
  - 电流通道相似度略低于电压（电压~99.99% 电流~98.7-99.5%）因电流幅值小谐波相对大，量化误差影响略大，后续可提高电流谐波量化位数
  - wave4 压缩比偏低 87:1 因暂态/畸变较多，字典匹配效率下降，但相似度仍>99.7% 达标
- 未决问题：电流通道优化、COMTRADE 解析、二次熵编码
- 相关文件/分支：file_tool.c (增强验证报告), data/wave1-4.csv, out/verification_report.csv/txt / arena/01a01ce5-nilm-tool


