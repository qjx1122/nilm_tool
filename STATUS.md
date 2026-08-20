# STATUS.md
## 当前目标
- 完成 v0.3 修复版三相波形压缩算法，验证相似度 >95% 且多周期压缩比 >300:1
- 固化文档与测试报告，完成收尾仪式并推送

## 已完成
- [x] 开局仪式：分支 arena/01a01ce5-nilm-tool，1 次提交 88462e1，工作区 clean，gh 已登录，gcc 12.2.0
- [x] 创建续接文件骨架：STATUS.md / session/NILM_AC_session_complete.md / README.md / REPORT.md / REPORT_TEST.md / setup.sh
- [x] 修复编译错误 printStats 未定义 -> 独立统计打印
- [x] 修复核心算法 bug：
  - harmCount 写入顺序错误（末尾写，解码先读）
  - 基波幅度量化范围错误：FFT 幅值 ~N/2 需归一化为 2*|F|/N，范围 0..2.0
  - 解码对称频点缺失
  - 零序优化评估错误：evaluate 固定调三相解压，零序包无法评估
- [x] v0.2 验证：单周期相似度 >99.8% 通过，压缩比 60-100:1（受限于 header 5字节开销）
- [x] v0.3 增强：
  - 扩展 HybridCompressor: prevMean/prevScale 支持高效重复帧
  - 实现 1字节重复标记 0xFF，周期稳态信号极致压缩
  - 重设计字典编码：包含基波幅相，残差相对量化，择优选择（字典更小时才用）
  - 重新启用帧间差分，阈值优化，支持多周期高压缩比演示
  - 新增多帧测试：10周期 384:1，50周期 548:1
- [x] 文档更新：README.md 补充 v0.3 结果，REPORT.md 稳定结论，REPORT_TEST.md 专题报告

## 进行中
- 无，准备收尾仪式

## 下一步（TODO）
1. 进一步优化 header 共享与三相联合编码，目标单周期 >200:1 且相似度 >97%
2. 增加真实现场数据测试（COMTRADE / CSV）及非平衡/暂态鲁棒性验证
3. 增加 Makefile 与 Python 绑定，便于与 NILM 模型集成
4. 设计二级压缩：先 FFT 粗压缩，再对量化后符号流用 Huffman / LZ4 二次压缩

## 决策记录 / 踩坑
- **决策：保留单文件 C 实现**：符合 BOOTSTRAP 文件治理，避免过早拆分
- **踩坑：printStats 未定义**：main 末尾调用 `comp.printStats` 但结构无函数指针，已替换为独立打印块
- **踩坑：幅值量化截断**：FFT 幅值 ~64 却用 0..1.5 量化，导致重构幅值 ~5V 而非 220V，RMSE 95，相似度负数。修复为归一化幅值 2*|F|/N 量化 0..2.0，解码还原 *N/2
- **踩坑：harmCount 顺序**：压缩端先写谐波再写数量，解码端先读数量，导致错位。已改为先写数量再写谐波
- **踩坑：字典幅值缩放**：字典系数 1.0 若直接作为 FFT 系数，未 *N/2 缩放，重构幅值过小。修复为字典系数 * fundAmpRaw，且包含基波幅相存储
- **踩坑：零序评估**：evaluate_compression 固定调用三相解压，零序压缩包用此评估相似度为负。已拆分为 evaluate_compression_zero
- **决策：高效重复帧**：稳态电网信号高度周期性，设计 1字节 REPEAT_MARKER 0xFF，当 diffEnergy<1e-6 且均值/尺度近似不变时直接复用前一帧，实现多周期平均压缩比 >300:1（10周期384，50周期548）
- **决策：字典择优**：仅当字典编码尺寸 < 标准编码尺寸时使用字典，避免字典增大压缩包
- **决策：frameDiff 默认启用**：v0.3 之后 create_config 默认 enableDict && enableFrameDiff true，单周期测试显式禁用以公平对比

## 关键文件路径
- 核心算法：`compress/compress_data.c` (v0.3, 约 900 行核心 + 测试，修复版)
- 引导协议：`BOOTSTRAP.md`
- 状态跟踪：`STATUS.md` (本文件)
- 会话纪要：`session/NILM_AC_session_complete.md`
- 专题报告：`REPORT_TEST.md`
- 稳定报告：`REPORT.md`
- 说明文档：`README.md`
- 构建脚本：`setup.sh`
- 原始说明：`readme.txt`
