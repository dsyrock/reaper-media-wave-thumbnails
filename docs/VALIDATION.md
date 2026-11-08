# 实测记录

测试日期：2026-11-08（Asia/Shanghai）。当前版本：0.3.0；REAPER 7.84/x64。

按 reaper-test 技能使用 `F:\AGENT\SKILLS\reaper-test\scripts\socket_client.py`，每次握手核对独立资源目录。日常使用的 `C:\REAPER` 未用于本轮测试。

## 0.3.0 实际首帧、真实素材与直接缓存读取

最终候选 DLL SHA-256：`56afdea833f918a10f290acd92ac6474c9e76684357de71cf1a620a13d191076`。

直接读取方案遵照 [Cockos 格式说明](https://www.reaper.fm/sdk/reapeaks.txt)。原生文件列表本身已开启 `LVS_EX_DOUBLEBUFFER`；新的数据发布机制在一个批次就绪后统一让列表重画。

- 从当前素材目录只读复制 32 个真实 WAV（合计 225,551,430 字节）及其已有 `.reapeaks` 到技能 Projects 下的 RealLibrary 测试目录。未改写原素材目录；测试素材不进入源码仓库或发布包。
- `tests/real-library-batch.lua`：清空扩展内存缓存再打开该目录，一页可见/预取共 28 个素材在首个波形绘制回调时全部就绪；扩展音频解码源打开数增加 0，源峰值 API 读取数增加 0。早期测量页面准备约 172ms；候选版本另一轮约 187ms。
- `tests/startup-batch.lua`：真正退出并确认测试进程结束，再启动同一实例；首个波形帧就绪 28 行、发布次数 1、解码源打开数 0、源峰值 API 读取数 0，列仍在最前且宽度 317。最终候选测量约 141ms。时间只指测试目录行集确定至发布的准备耗时，且测试读取的是复制到 F 盘的素材缓存，不是整个 REAPER 启动耗时，也不代表所有磁盘性能。
- `tests/reader-selftest.cpp`：RPKM/RPKN/RPKL、立体声混合、短文件零样本粗层、频谱/响度附加层、分辨率选取、末尾瞬态补齐、源属性不匹配、截断/溢出/非法峰值均通过；读取队列取消、关闭和再启动通过。
- `tests/cache-session.lua`：640 文件缓存不淘汰、返回早期目录不重新打开源/读取 API、位图复用/预算、宽度变化复用峰值、源文件变化后单独更新均通过。此轮还等待实际绘制回调，避免只凭内部 ready 计数就滚到下一页。
- `tests/columns.lua`、`tests/regression.lua`、`tests/cold-cache.lua`、`tests/peak-refresh.lua`、`tests/database.lua`、`tests/search.lua` 的功能回归通过。

首版解析器把短音频的零样本粗层当作无效格式，导致回退源 API；已修正并加入针对性测试。首帧测试也改为等待真正绘制回调后检查，避免把“数据已发布但尚未绘制”的状态误判为失败。

不增加波形磁盘缓存；重启仍发生缓存文件读取和位图绘制。改进是避开解码器并按页显示，而非跨进程保存屏幕像素。缺失或无法直接使用的缓存仍可能回退/构建，未承诺这些情况也无等待。

## 0.2.2 自有列布局与重启读取回归

最终候选 DLL SHA-256：`4a3a8ae7607047034bb1f450867b9fb0cfb58e0c614751a423212b120c47aa13`。

修复前 0.2.1 的布局测试只覆盖了原生列，遗漏了 Waveform 自身的位置与宽度；其插入位置在启动时仍被固定放回 File 后。0.2.2 为自有列保存独立设置。

- `tests/restart-layout-save.lua`：把 Waveform 移到显示第 1 列，宽度设置为 317，确认 ExtState 写入 `wave_order=0`、`wave_width=317`，同时记录完整原生列布局。
- 正常退出实际测试进程，确认结束后启动同一个 REAPER；`tests/restart-layout-check.lua` 断言 Waveform 仍为第 1 列、宽度 317；其余全部原生列顺序和宽度与退出前一致。
- 同一重启测试中的 28 个可见音频全部直接读取到现有峰值：`fast_peak_hits=28`，`peak_build_probes=0`、`peak_build_starts=0`。此结论是未进入构建流程，不是没有读取文件或没有重绘位图。
- `tests/peak-refresh.lua`：同大小、不同修改时间的 1 秒 WAV 从峰值约 0.8 改为约 0.2，断言波形及时更新且只重新打开变化的源文件；未变化的 Keep.wav 继续使用已有数组。
- 直接读峰值的初版在文件修改后读到旧 `.reapeaks`，仅调用 BuildPeaks 或清理内存引用也未更新；修正为对已确认变化/过期的对应派生峰值文件执行失效处理，再构建，原故障回归通过。无法使过期文件失效时返回明确错误并留空，不显示旧波形。
- 最终候选重跑 `tests/columns.lua`、`tests/regression.lua`、`tests/database.lua`、`tests/search.lua`，全部通过；先前的冷缓存测试 `tests/cold-cache.lua` 也在本轮读取路径改动后通过。

没有增加扩展自建的峰值或位图磁盘缓存。布局设置写入 REAPER ExtState；生产环境新增的峰值文件仍由 REAPER 自身生成。

## 0.2.1 Temporary Mark / 原生列布局回归

实测 DLL SHA-256：`7b06f9e5e57c272925e3c76ef1294f37ce0bb65b85ab619f2c0ade5caac53c54`。

- 在 0.2.0 下复现：补丁启用时，对原生 25 列应用顺序数组失败；它的控件此时已有 26 列。此类失败会阻止原生列布局恢复。
- `tests/columns.lua`：设置 Mark 最前、Fav 第二、Mark 宽度 56，断言全部原生列标题、相对顺序、宽度及原有零宽隐藏列保持不变；Waveform 跟在实际 File 列之后。原生 25 列布局应用成功；原生列删除/重建后仍完整恢复；重复开关不丢列、不重复附加。
- `tests/columns-startup.lua`：在独立测试配置中保存 Mark 最前、宽度 56 的布局，正常退出并重启后仍正确恢复。
- 重跑 `tests/regression.lua`，多行绘制、Unicode、单双声道、静音、坏文件、同名文件与缓存复用均通过。
- 启动测试的 INI 准备首次将顺序数组写反；修正为 `colN_order = 第 N 个显示位置的逻辑列号` 后重跑通过。

## 0.2.0 缓存优化回归

已部署并实测 DLL SHA-256：`b5bd2b6ac74a565a53da93a7c8c30e9ece3e60ad34af87251907eabdce435e91`。

`tests/cache-session.lua` 生成 640 个独立 WAV，通过原生列表逐页访问，断言并通过：

- 访问完后缓存仍保留 640 个文件，峰值数组合计 983,040 字节，早期文件未被淘汰。
- 切换到另一目录再返回最早一页：补丁的 `source_opens`、`peak_reads` 和 `builds` 均增加 0。此轮行映射恢复诊断为 31ms；这是本次事件至映射完成的测量，不代表所有设备/素材库的总显示耗时。
- 普通列表重绘增加位图命中数，位图生成数和峰值读取数不增加。
- 改变波形列宽会生成新尺寸的位图，峰值读取数不增加。
- 同大小 WAV 内容和修改时间变化后，只有该文件重新读取一次峰值，并更新对应位图。
- 位图缓存维持在 256 张以内、16 MiB 估算像素量以内；测试经历了实际位图淘汰，但没有引起峰值缓存淘汰。

本轮另外重跑 `tests/regression.lua`、`tests/database.lua`、`tests/search.lua`，均通过。覆盖中文、单/双声道、静音、坏文件、同名文件、重复开关、数据库显示偏好恢复，以及搜索/清空搜索。

640 文件测试初次在末页使用了超出实际列表条目数的滚动索引；已根据实际条目数截断后重跑，完整测试返回 `CACHE_SESSION_ALL_PASSED`。

## 0.1.0 原始功能验证

| 入口 | 检查 |
| --- | --- |
| `tests/fixtures.lua`、`tests/smoke.lua` | 多个文件同时获得波形，Unicode 文件可见，实际 custom draw 次数大于零 |
| `tests/regression.lua` | 单声道峰值约 0.795、立体声约 0.498、静音为 0；滚动后第 40 个文件继续生成；损坏 WAV 留空；另一个目录同名文件峰值约 0.199；关闭后原生 25 列恢复；多次开关不重复；返回已访问目录复用缓存 |
| `tests/database-setup.lua`、`tests/configure-db.ps1`、`tests/database.lua` | 原生素材库的 3 行，包括两个不同目录同名音频，峰值各自正确；完整路径和前导路径偏好保持原值；清空缓存后重建成功 |
| `tests/search.lua` | 素材库搜索 stereo 后只显示对应音频的波形；清除搜索恢复 3 行 |
| `tests/cold-cache.lua` | 8 个全新文件首次生成峰值，包括当前选中预览文件，均获得缩略图 |
| `tests/capture.lua` | 通过 JS GDI/LICE 获取实际 Media Explorer 画面并人工查看，原生表头及多行波形可见 |

构建使用 `g++ 15.2.0`，`-Wall -Wextra` 无警告。检查 DLL 导入，除 Windows 系统组件外没有 MinGW、C++ 或 pthread 动态运行库依赖。

## 故障与修复

- 首次峰值缓存构建可能暂时返回 0 点：增加间隔 500ms、最多 5 次的空读重试，冷缓存测试通过。
- 测试目录传给 OpenMediaExplorer 时会定位到父目录：测试改为传入具体音频文件。
- 数据库准备脚本将 Lua `gsub` 的两个返回值传给 JS API，触发参数错误；改为离线注册专用测试库，重启接收器后通过。
- 冷缓存测试中 Lua `%d` 接收非整数：对时间戳取整后重跑通过。

最初一次在沙箱账户下启动的测试 REAPER 未能连接桥接；后续全部功能断言均在正常用户下启动并通过资源目录握手的独立实例执行。

## 未覆盖

不同 REAPER 版本、macOS/Linux、大型素材库压力测试、所有编解码格式、实际试听听感、鼠标拖拽、所有表头菜单/排序组合、第三方列表绘制扩展及异常退出场景未全面验证。没有将“脚本无报错”等同于这些行为已通过。
