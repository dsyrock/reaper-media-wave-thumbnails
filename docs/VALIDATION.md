# 实测记录

测试日期：2026-11-08（Asia/Shanghai）。版本：0.1.0；REAPER 7.84/x64。

按 reaper-test 技能使用 `F:\AGENT\SKILLS\reaper-test\scripts\socket_client.py`，每次握手核对独立资源目录。日常使用的 `C:\REAPER` 未用于本轮测试。

## 通过的功能断言

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
