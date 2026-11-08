# 实测记录

测试日期：2026-11-08（Asia/Shanghai）。当前版本：0.2.2；REAPER 7.84/x64。

按 reaper-test 技能使用 `F:\AGENT\SKILLS\reaper-test\scripts\socket_client.py`，每次握手核对独立资源目录。日常使用的 `C:\REAPER` 未用于本轮测试。

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
