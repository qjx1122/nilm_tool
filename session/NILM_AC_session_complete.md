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

