# Media Explorer 波形列研究

## 结论

可通过独立 REAPER 扩展实现原生列表的逐行波形，无需修改程序二进制。公开 SDK 提供音频峰值及解码能力，但没有专门的 Media Explorer 自定义列接口；Windows 列表挂接是本原型的实现选择。

## 目标和证据

- 目标 `K:\REAPER-test\reaper.exe`，REAPER 7.84 x64。SHA-256：`43293d1e6b51b7e27d53a09cee6cfcfd746438cf7f3866969b99cd1afa0916fc`。
- Media Explorer 实现在 `Plugins/reaper_explorer.dll`。SHA-256：`6b24d16b1d920c9f0b5649d392e289362d828fc40858fcd9a2e0ace9de4f492e`。
- reaper-test 技能独立环境的 EXE、Explorer DLL 与上述两个文件分别逐字节哈希一致；实测环境因此对应本轮目标版本。
- REA `inspect_pe_resources` 的 Evidence ID：`ev_8f6becd5dfed4886cd1db9cc98dcc6c63f0634839f427302e357d30d7a2c3660`。完整证据保存在 `rea-resources.json`，资源表 19 个条目、覆盖完整。
- 在 REA 确定的资源范围上，`inspect-dialogs.mjs` 校验原文件哈希并解码对话框 101；输出见 `dialogs.json`。列表 ID 1001，`SysListView32`，静态 style `0x52811049`（含 `LVS_OWNERDATA`）；路径下拉框 1002；搜索框 1015。此解码属于本地辅助工具的派生结果，非新增 REA Evidence。
- 实机经 reaper-test 桥接确认相同控件结构；增加前原生表头 25 列，增加后 26 列。

REA Ghidra 深度字符串分析中途出现 `Transport closed`，随后会话查询也失联。未获得可引用的函数反编译/调用图证据，也没有将静态资源推断冒充代码执行。后续使用已安装 REA 的公共 CLI 重新取得同一资源 Evidence；没有改动 MCP 配置或安装分析引擎。

## 实现

1. 主线程 timer 寻找已存在的 Media Explorer；不会主动弹出窗口。
2. `SetWindowSubclass` 接入父窗口 `WM_NOTIFY`，增加一个末尾逻辑列，并将其显示顺序放在文件名之后，保留原有 subitem 索引。
3. 虚拟列表文本通过原生 `LVN_GETDISPINFO` 取得；拦截本扩展列的通知，避免交给原处理器未知的列索引。
4. `NM_CUSTOMDRAW` 只绘制本扩展列；其他通知继续进入原生处理器。原列表改变条目数或排序时清除行映射。
5. 使用 `PCM_Source_CreateFromFileEx` / `PCM_Source_BuildPeaks` / `PCM_Source_GetPeaks`，每段 192 个峰值点，混合各声道上下包络。每次 timer 只启动一个文件或推进一次峰值构建，优先可见行。
6. 缓存键为完整路径，并校验大小与修改时间。数据库同名文件借原生完整路径显示取得实际路径，随后恢复显示偏好。
7. 关闭功能时删除自有列、移除 subclass 并释放本扩展持有的 source。

## 研究问题状态

| 问题 | 状态 | 证据边界 |
| --- | --- | --- |
| 是否有可逐行显示的原生列表 | 已回答 | REA 资源 + 派生解码 + 实机 HWND/列数 |
| 是否能获取全长音频波形 | 已回答 | Cockos SDK + 单/双声道/静音/冷缓存实测 |
| 数据库和搜索行能否定位真实文件 | 已回答 | 同名不同目录数据库、过滤及恢复偏好断言 |
| 是否保留整个原生交互行为 | 部分回答 | 列恢复、滚动、搜索、重复开关已测；试听听感和拖拽等未全面测试 |
| 是否跨版本稳定 | 未回答 | 只验证 7.84 x64；内部控件结构依赖仍在 |
| 是否能直接二进制打补丁 | 未研究完成 | 未取得函数级深度证据；已通过扩展方式实现功能 |

## 主要资料

- [Cockos 扩展 SDK](https://www.reaper.fm/sdk/plugin/plugin.php)
- [Cockos PCM_Source_GetPeaks 文档](https://www.reaper.fm/sdk/reascript/reascripthelp.html#PCM_Source_GetPeaks)
- [Cockos PCM_Source_BuildPeaks 文档](https://www.reaper.fm/sdk/reascript/reascripthelp.html#PCM_Source_BuildPeaks)
- [Microsoft NM_CUSTOMDRAW](https://learn.microsoft.com/en-us/windows/win32/controls/nm-customdraw-list-view)
- [Microsoft SetWindowSubclass](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-setwindowsubclass)
- [Microsoft 虚拟列表文本限制](https://learn.microsoft.com/en-us/windows/win32/controls/lvm-getitemtext)

本机现有脚本 `C:\REAPER\Scripts\FTC Tools\ReaSamplOmatic5000\Link media explorer to active sample player.lua` 也使用控件 1001/1002、扩展名列 3，以及命令 42026/42134 解析数据库选中项路径；本扩展以本轮实机证据验证后使用相同原生机制。
