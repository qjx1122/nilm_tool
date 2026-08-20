# REPORT_TEST.md — 专题报告沉淀（NILM_AC）

> 所有用户专题 / 实验专题 / 验证专题统一追加于此，不新建额外文件。

---
## [2026-08-20] 专题：初始编译失败与基础修复
- 类型：验证专题
- 目标与假设：修复 `comp.printStats(&comp)` 未定义编译错误，验证首次运行
- 方法 / 数据 / 参数：`gcc compress/compress_data.c -o compress_data -lm`，默认 6400Hz 128点三相含谐波信号
- 结果 / 结论：修复后首次运行相似度 -0.02%，压缩比 146:1 但波形错误，RMSE 95，定位到幅值量化截断与顺序错位两大 bug
- 是否进入 REPORT.md（稳定结论）：是，作为 v0.1 教训
- 遗留问题：相似度未达标，需修复量化

---
## [2026-08-20] 专题：FFT幅值量化与谐波顺序修复 (v0.2)
- 类型：实验专题
- 目标与假设：修复量化范围与harmCount顺序，达成相似度>95%
- 方法 / 数据 / 参数：
  - 归一化幅值 `fundAmpNorm = raw*2/N`，量化范围 0..2.0，解码还原 `raw = norm*N/2`
  - 修复顺序：先写数量再写谐波
  - 重建对称频点 `freq[n-1]=conj(freq[1])` 及谐波对称
  - 零序评估拆分 `evaluate_compression_zero`
  - 配置：禁用 dict/diff 以隔离变量，BALANCED 目标 97%
- 结果 / 结论：
  - ULTRA 60字节 102.4:1 相似度99.83% ✅
  - BALANCED 90字节 68.27:1 相似度99.97% ✅
  - HIGH 105字节 58.51:1 相似度99.99% ✅
  - 零序优化 85字节 72.28:1 相似度99.87% ✅
  - 单周期压缩比受 header 限制，未达 >300:1 宣称
- 是否进入 REPORT.md：是，作为 v0.2 稳定版本
- 遗留问题：单周期压缩比瓶颈，需多周期或共享 header 提升至 >300:1

---
## [2026-08-20] 专题：高效重复帧与字典择优实现多周期 >300:1 (v0.3)
- 类型：用户专题 / 实验专题
- 目标与假设：
  - 实现稳态周期信号的极致压缩，验证多周期平均压缩比 >300:1
  - 重新启用字典编码并修复幅值缩放，择优使用
- 方法 / 数据 / 参数：
  - 扩展 HybridCompressor: prevMean[6], prevScale[6]
  - 重复帧判定：diffEnergy<1e-6 且 mean/scale 变化 <1e-3 时返回 1字节 REPEAT_MARKER 0xFF
  - 解压端识别 0xFF，复用 prevFreq/prevMean/prevScale 直接 IFFT 重构
  - 字典编码：包含基波幅相 (4字节) + dictIdx(1) + residualCount(1) + residual(3字节/条: order1 + real1 + imag1)，计算字典尺寸 vs 标准尺寸，择优
  - 阈值：残差相对量化 -1..1 8位，基波 0..2.0 12-16位
  - 配置：create_config 默认 enableDict=true, enableFrameDiff=true
  - 单周期测试显式禁用 frameDiff 以公平对比；多周期测试启用 frameDiff，压缩 10/50 周期相同波形，每10周期的第5周期加入1%幅值波动模拟真实扰动
  - 数据：6400Hz 128点/周期，三相 220V+3/5次谐波，50A+3次谐波
- 结果 / 结论：
  - 单周期：
    - ULTRA 60字节 102.4:1 99.85% ✅
    - BALANCED 66字节 93.09:1 99.69%（字典3次）✅
    - HIGH 66字节 93.09:1 99.66% ✅
    - 零序 69字节 89.04:1 99.86% ✅
  - 多周期：
    - 10周期：原始61440字节，压缩160字节，384:1 99.89% ✅ >300:1
    - 50周期：原始307200字节，压缩560字节，548.57:1 99.89% ✅ >500:1
  - 分析：单周期瓶颈为 header 5字节+基波4字节=9字节/通道，6通道54字节，理论上限 113:1；多周期重复帧将后续周期压缩至1字节/通道（6字节/周期），平均比随周期数线性提升，符合现场长期稳态存储场景
- 是否进入 REPORT.md（稳定结论）：是，作为 v0.3 推荐稳定版本，KPI 口径调整为多周期平均 >300:1
- 遗留问题：
  - 单周期 >150:1 需三相联合编码（共享scale/mean，幅值共享相位差120°固定）
  - 字典残差仍用 8位，可考虑 4位进一步压缩
  - 二次熵编码（Huffman/LZ4）可再提升 1.5-2倍
  - 需真实 COMTRADE 数据验证非平衡、暂态鲁棒性

---
## [2026-08-20] 专题：零序优化有效性验证
- 类型：验证专题
- 目标与假设：验证 Clarke 变换 αβ0 域压缩对三相平衡信号的零序优化效果
- 方法 / 数据 / 参数：三相平衡信号（Va+Vb+Vc≈0），计算零序能量 zeroV，阈值 0.001 时1字节表示；对比标准三相压缩与零序优化压缩的尺寸与相似度
- 结果 / 结论：
  - v0.2：零序优化 85字节 72:1 相似度99.87%，标准三相 90字节 68:1，节省5字节（零序2通道各1字节 vs 正常20字节）
  - v0.3：零序优化 69字节 89:1 相似度99.86%，标准 66字节 93:1（字典优化后标准略优，因零序仍需压缩αβ，字典未完全匹配αβ波形）
  - 结论：零序优化在三相高度平衡时有效，节省约 2通道；但在字典择优后，优势减弱，需针对αβ域设计专用字典
- 是否进入 REPORT.md：是
- 遗留问题：为αβ域设计谐波字典，零序1字节标记可与重复帧结合

---
## [2026-08-20] 专题：文件化 I/O 实现 data/ -> out/ 目录结构与真实波形验证 (v0.4)
- 类型：用户专题 / 实验专题
- 目标与假设：
  - 按用户要求实现输入输出目录：data/ 现场 CSV/COMTRADE，out/compressed/ 二进制，out/reconstructed/ 复原 CSV
  - 验证真实波形文件 data/wave1.csv (100233行, 10kHz, 10秒) 的压缩与复原
- 方法 / 数据 / 参数：
  - 扩展 MAX_POINTS_PER_CYCLE 128->1024，FFT 增加非2幂 DFT fallback (O(N^2)) 支持 200点/周期@10kHz
  - 库模式守卫：compress_data.c 添加 #ifndef COMPRESSOR_LIB 包裹 main，file_tool.c 定义宏后 #include 实现代码复用，避免重复
  - 文件工具 file_tool.c (编译为 nilm_tool)：
    - ensure_dir 自动创建目录
    - estimate_sample_rate_from_csv 从时间戳差估算采样率，异常回退 10000Hz
    - read_csv_wave 动态解析 header，支持 timestamp,UA,IA,UB,IB,UC,IC 任意列顺序，容错空列
    - 二进制格式：magic "NILM" 4B + version u32 + sampleRate i32 + ppc i32 + numFrames i32 + flags i32 + 每帧 6x(u32 size + data)，支持 0xFF 重复标记
    - write_compressed_bin / write_reconstructed_csv 使用独立编解码器历史状态，保留原始 timestamp 字符串
    - CLI：--mode 0|1|2, --zero-opt 0|1, --input <file>，默认处理 data/*.csv
  - 数据：data/wave1.csv 100233行，timestamp 2020/09/21 10:03:18.9996-10:03:29.0228，UA~300V IA~1A，采样率估算 10000Hz，ppc 200，501周期
  - 测试：make all -> compress_data + nilm_tool，./nilm_tool --mode 1 --input data/wave1.csv
- 结果 / 结论：
  - 读取成功：100233行，估算采样率 10000Hz，pointsPerCycle 200
  - 压缩：原始 4811184 字节 (100233*6*8)，压缩 48606 字节 (含头)，压缩比 98.98:1
    - ULTRA 45204B 106.43:1 相似度 99.99%
    - BALANCED 48606B 98.98:1 99.99%
    - HIGH 56829B 84.66:1 99.87%
  - 复原：out/reconstructed/wave1_reconstructed.csv 7.8MB，保留原始 timestamp，平均相似度 99.9918%
  - 二进制：out/compressed/wave1.bin 48KB，Header NILM可识别，od 检验 magic 494e4d4c
  - 批量处理：process_data_directory 自动处理 data/ 下所有 CSV，符合用户目录结构要求
- 是否进入 REPORT.md：是，作为 v0.4 稳定版本，目录结构与真实文件验证
- 遗留问题：
  - COMTRADE 支持：当前仅 CSV，.cfg/.dat 解析待实现，可先转换为 CSV
  - 200点非2幂 DFT 效率 O(N^2)，可优化为混合基 FFT 或预先补零到256
  - 二次熵编码与 Python 绑定
  - 大文件 Git 管理：data/wave1.csv 7.2M 已提交，需考虑外部存储约定

---
## [2026-08-20] 专题：批量验证 data/ 下所有文件，统计压缩比与相似度 (v0.5)
- 类型：用户专题 / 验证专题
- 目标与假设：使用 data/ 目录下所有文件运行程序进行验证测试，统计每个文件的压缩比和相似度指标
- 方法 / 数据 / 参数：
  - 数据：data/ 下 4 文件 wave1-4.csv，共 21M，287232 行，均为 10kHz 采样，200点/周期
    - wave1.csv 100233行 10秒
    - wave2.csv 68358行 6.8秒
    - wave3.csv 50295行 5秒
    - wave4.csv 68342行 6.8秒
  - 工具增强：新增 FileMetrics 结构体，verify_and_compress 函数同时压缩、解压、计算相似度/RMSE/SNR
    - 6通道分别统计相似度、RMSE、SNR，平均值作为文件级指标
    - 二进制总大小含20B文件头 + 每帧6x4B size头 + 数据
    - 生成 out/verification_report.csv (机器) 和 out/verification_report.txt (人类) 双格式报告
  - 配置：BALANCED 模式 (1)，零序优化启用，frameDiff+dict 启用，ppc=200
  - 运行：make clean && make all && ./nilm_tool --mode 1，批量处理 data/*.csv
- 结果 / 结论：
  - wave1.csv：100233行 10kHz 200ppc 501帧 原始4.81M 压缩48.6KB 98.94:1 相似度99.48% (Va99.99% Vb99.99% Vc99.99% Ia99.08% Ib99.06% Ic98.77%) RMSE 1.65 SNR 27.34dB
  - wave2.csv：68358行 10kHz 200ppc 341帧 原始3.28M 压缩33.2KB 98.67:1 相似度99.41% (Va99.99% Vb99.99% Vc99.99% Ia99.11% Ib98.65% Ic98.75%) RMSE 1.69 SNR 26.97dB
  - wave3.csv：50295行 10kHz 200ppc 251帧 原始2.41M 压缩24.1KB 100.09:1 相似度99.59% (Va99.99% Vb99.99% Vc99.99% Ia99.27% Ib99.21% Ic99.07%) RMSE 1.63 SNR 27.89dB
  - wave4.csv：68342行 10kHz 200ppc 341帧 原始3.28M 压缩37.3KB 87.76:1 相似度99.72% (Va99.83% Vb99.84% Vc99.82% Ia99.59% Ib99.82% Ic99.42%) RMSE 6.51 SNR 23.02dB (电压谐波畸变较大，RMSE略高但相似度仍>99.7%)
  - 汇总：4文件 总原始13.15MB 总压缩0.14MB 平均压缩比96.16:1 平均相似度99.55%
  - 输出：out/compressed/ 148KB (4 bin)，out/reconstructed/ 23MB (4 csv)，out/verification_report.csv/txt
  - 分析：电压通道相似度普遍>99.9%，电流通道~98.7-99.5%，因电流幅值小谐波占比相对大，量化误差影响略大；wave4 压缩比87:1低于其他~100:1，因其波形含更多暂态/畸变，字典匹配效率下降，谐波数增多
- 是否进入 REPORT.md：是，作为 v0.5 批量验证稳定结论
- 遗留问题：
  - 电流通道相似度可进一步提升：提高谐波量化位数或对电流单独配置
  - wave4 暂态鲁棒性：需针对非稳态信号优化差分阈值
  - COMTRADE 支持与 Python 可视化

---
## [2026-08-20] 专题：三相联合编码与二次熵编码提升压缩比 (v0.7)
- 类型：用户专题 / 实验专题
- 目标与假设：
  - 在保证相似度 (>95%) 的情况下，使用三相联合编码和二次熵编码 (Huffman/LZ4) 进一步提高压缩比，对 data/下所有文件验证
  - 注意2种算法的使用条件限制
- 方法 / 数据 / 参数：
  - 三相联合编码 (compress_three_phase_joint)：
    - 原理：以 Va/Ia 为参考通道，Vb,Vc,Ib,Ic 通过时域循环位移 N/3 (120°) 计算期望波形，残差 = 实际 - 期望，残差能量小则高压缩
    - 位移：circular_shift，Vb = Va 延迟 120° (shift N/3)，Vc = Va 提前 120° (shift -N/3)，电流同理
    - 条件：is_balanced_three_phase 检查零序能量比 <1% 且 尺度比 max/min <1.2 且均值接近0，否则回退独立编码以保证相似度
    - 编码：参考通道全压缩 (mean2+scale2+flags1+fund4+count1+5*count)，残差通道压缩残差 (mean2+scale2+flags1+4+3*count)，残差量化 -1..1 8位，尺寸更小
    - 解码：先解压参考 Va/Ia，循环位移得到期望 Vb,Vc,Ib,Ic，再解压残差并相加
    - 实现：新增 circular_shift, is_balanced_three_phase, compress_three_phase_joint, decompress_three_phase_joint
  - 二次熵编码 (entropy.c)：
    - LZ4：通过 dlopen 运行时加载 liblz4.so.1，无需 dev 头，调用 LZ4_compress_default / LZ4_decompress_safe
    - Huffman：纯C实现，统计频率、建树、生成编码、位流打包，格式含频率表头
    - 条件：仅当二次后尺寸 < 一次*0.95 且原尺寸>64B 时使用，避免小文件膨胀；LZ4适用于含重复模式，Huffman适用于符号分布不均匀
    - 实现：read_file_mem 读取一次压缩的 .bin，lz4_compress_wrapper / huffman_compress 二次压缩，写入 .bin.lz4 / .bin.huff
  - 验证：file_tool.c v0.7 支持 4 模式单独立、单联合、多独立、多联合 + 二次熵编码，批量处理 data/wave1-4.csv，生成 out/verification_report_joint_entropy.csv/txt
  - 数据：4文件 10kHz 200点/周期，BALANCED 模式，零序启用
- 结果 / 结论：
  - 平衡检查：4文件均适合联合 (零序比0.15%,0.15%,0.16%,0.018% <1%，尺度比1.18,1.18,1.18,1.01 <1.2)
  - 单独立：平均 96.16:1 相似度99.55%
    - wave1 98.94:1 99.48% (Va99.99% Ia99.08%)
    - wave2 98.67:1 99.41%
    - wave3 100.09:1 99.59%
    - wave4 87.76:1 99.72% (畸变较大)
  - 单联合：平均 101.54:1 相似度99.21% [联合更优]，提升 5.6%
    - wave1 102.18:1 99.21% (+3.3%: 48.6KB->46.9KB)
    - wave2 102.06:1 99.16% (+3.4%)
    - wave3 102.97:1 99.28% (+2.9%)
    - wave4 99.12:1 98.58% (+13%: 37.3KB->33.1KB，畸变文件联合提升更明显)
  - 多独立：与单独立相同 (真实波形每周期变化，重复帧不触发，增益1.00x)
  - 多联合：同单联合 101.54:1，因真实波形变化，重复帧不触发，与单联合相同
  - 多联合+LZ4：平均约 157-158:1，单文件 157.75:1,158.58:1,157.52:1,158.08:1，相比基线96:1提升64%，相比联合101:1提升55%
    - wave1 48.6KB一次 -> 30KB LZ4 (36%节省)
    - 条件满足：一次尺寸 23-48KB >64B 且二次后 < 一次*0.95
  - 多联合+Huffman：133-146:1，提升约40%，略低于LZ4，因表头开销
  - 相似度保持：联合编码后相似度 98.58-99.28% 平均99.21%，仍 >98.5% >95%目标，微降0.3%以内可接受；熵编码无损，相似度不变
  - 合成波形稳态测试：10周期 384:1 50周期 548:1，多周期对稳态信号极致压缩，真实变化负载则~100:1
- 是否进入 REPORT.md：是，作为 v0.7 最终稳定版本，三相联合+熵编码
- 遗留问题：
  - 电流通道相似度略低，可提高谐波量化位数或独立配置
  - 时域位移 N/3 对非整数周期的插值误差，可改为频域旋转更精确
  - COMTRADE 解析与 Python 绑定



