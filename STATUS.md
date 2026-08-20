# STATUS.md
## 当前目标
- 在保证相似度的情况下，使用三相联合编码和二次熵编码（Huffman/LZ4）进一步提高压缩比，并对 data/ 下所有文件进行验证统计

## 已完成
- [x] 拉取最新代码：远程 daac82b 已包含 file_tool v0.6 单/多周期验证
- [x] 实现三相联合编码 (compress_three_phase_joint)：
  - 原理：以 Va/Ia 为参考，Vb,Vc,Ib,Ic 编码为时域循环位移残差（N/3 = 120°），适用于三相平衡
  - 条件：零序能量比 <1% 且 尺度比 max/min <1.2 且均值接近0，否则回退独立编码
  - 实现：circular_shift，参考通道全压缩，残差通道压缩残差，残差能量小则高压缩比
  - 效果：wave1-4 均满足平衡条件，联合编码比独立提升 3-13%（单独立96.16:1 -> 单联合101.54:1）
  - 相似度保持：独立99.55% -> 联合99.21% 平均，仍 >98.5% >95%目标，允许微降
- [x] 实现二次熵编码 (entropy.c)：
  - LZ4 via dlopen：运行时加载 liblz4.so.1，调用 LZ4_compress_default / LZ4_decompress_safe，无需 dev 头
  - Huffman：纯C实现，统计频率、建树、生成编码、位流打包，格式含频率表头
  - 条件：仅当二次压缩后尺寸 < 一次*0.95 且原尺寸>64B 时使用，避免小文件膨胀
  - 效果：对多周期联合的 .bin 文件，LZ4 平均从 22-47KB -> 15-30KB，压缩比从 101:1 -> 157:1，提升 ~55%；Huffman 133-146:1，提升 ~40%
- [x] 增强 file_tool.c v0.7：
  - 支持 4 种模式：单独立、单联合、多独立、多联合
  - 每种模式后尝试 LZ4 和 Huffman 二次压缩，生成 .bin.lz4 和 .bin.huff
  - 生成 out/verification_report_joint_entropy.csv/txt，含单/多、独立/联合、LZ4/Huffman、零序比/尺度比、是否使用联合等
- [x] 批量验证 data/下4文件 (BALANCED)：
  - wave1：单独立98.94:1 99.48% -> 单联合102.18:1 99.21% [联合更优] -> 多联合102.18:1 -> +LZ4 157.75:1 (零序0.15% 尺度比1.18 适合联合)
  - wave2：98.67:1 99.41% -> 102.06:1 99.16% -> +LZ4 158.58:1
  - wave3：100.09:1 99.59% -> 102.97:1 99.28% -> +LZ4 157.52:1
  - wave4：87.76:1 99.72% -> 99.12:1 98.58% -> +LZ4 158.08:1 (不平衡度稍高但仍适合联合，提升13%)
  - 汇总：单独立96.16:1 -> 单联合101.54:1 (+5.6%) -> 多联合101.54:1 -> +LZ4 平均约157:1 (+64% over baseline)
  - 相似度保持 >98.5%，满足 >95% 要求
- [x] Makefile 增加 -ldl，entropy.c 编译，setup.sh 更新
- [x] 生成报告：out/verification_report_joint_entropy.csv/txt 已提交至根目录

## 进行中
- 更新 README, REPORT, REPORT_TEST 文档，准备提交推送

## 下一步（TODO）
1. COMTRADE 解析
2. 针对电流通道相似度优化（提高谐波量化位数）
3. 三相联合中谐波旋转的精确处理（当前仅时域位移，频域旋转可更精确）
4. Python 绑定与可视化

## 决策记录 / 踩坑
- **决策：时域位移而非频域旋转**：频域旋转需处理正负零序，时域循环位移 N/3 实现简单，对平衡正弦波精确，实测残差小，压缩比提升明显
- **踩坑：多周期增益消失**：真实波形每周期均有变化，重复帧不触发，单与多周期比相同（1.00x），说明多周期对稳态合成波有效，对真实变化负载效果有限
- **决策：二次熵编码条件**：必须检查二次后 < 一次*0.95 且 >64B，否则小文件膨胀。对 20-50KB 的 .bin 文件，LZ4 平均节省 35%，Huffman 25%
- **踩坑：LZ4 dlopen**：无 dev 头，通过 dlopen 运行时加载 liblz4.so.1，避免编译依赖，兼容性好
- **决策：联合回退**：若不平衡（零序>1% 或 尺度比>1.2），回退独立编码以保证相似度，4个测试文件均平衡，联合均更优

## 关键文件路径
- 核心：`compress/compress_data.c` (新增 joint 函数，is_balanced, circular_shift)
- 熵编码：`entropy.c` (LZ4 via dlopen + Huffman)
- 工具：`file_tool.c` v0.7 (单/多、独立/联合、LZ4/Huffman)
- 数据：`data/wave1-4.csv` (21M)
- 输出：`out/compressed/*_single.bin`, `*_single_joint.bin`, `*_multi.bin`, `*_multi_joint.bin`, `*.lz4`, `*.huff` (总计约 16 文件)
- 报告：`out/verification_report_joint_entropy.csv/txt` + 根目录拷贝
- 构建：`Makefile` (-lm -ldl)
