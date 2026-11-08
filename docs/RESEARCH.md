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
2. `SetWindowSubclass` 接入父窗口 `WM_NOTIFY`，增加一个末尾逻辑列，保留原有 subitem 索引。显示位置和宽度使用单独的 ExtState 设置；首次无设置时才放在文件名之后。
3. 虚拟列表文本通过原生 `LVN_GETDISPINFO` 取得；拦截本扩展列的通知，避免交给原处理器未知的列索引。
4. `NM_CUSTOMDRAW` 只绘制本扩展列；其他通知继续进入原生处理器。原列表改变条目数或排序时清除行映射。
5. 使用 `PCM_Source_CreateFromFileEx` / `PCM_Source_BuildPeaks` / `PCM_Source_GetPeaks`，每段 192 个峰值点，混合各声道上下包络。0.2.0 将短峰值读取按目标 4ms、最多 32 个处理步骤批量执行；单次 REAPER 调用仍可能超过此预算。命中峰值缓存的行不经过该队列。
6. 缓存键为完整路径，并校验大小与修改时间。峰值数组在本次运行中保留，不按条数或时间淘汰。数据库同名文件借原生完整路径显示取得实际路径，随后恢复显示偏好。
7. 关闭功能时删除自有列、移除 subclass 并释放本扩展持有的 source。
8. 0.2.0 增加独立的 GDI 位图 LRU（最多 256 张、16 MiB 估算像素量），以文件修订、宽高、前景/背景颜色区分。位图淘汰不影响峰值缓存；文件内容变化时清除对应位图；卸载释放全部位图及内存 DC。
9. 0.2.1 修正原生列布局兼容性：附加列使列表由 25 列变为 26 列，而原生 25 列的 `LVM_SETCOLUMNORDERARRAY` 请求会在旧版补丁下失败。新版本在原生列调整前撤下波形列，完成后延迟重新附加；关闭窗口时也在原生保存偏好前撤下自有列。使用独立 Header lParam 标识自有列，避免列号变化时误认原生列；插入位置依据实际 File 列，不固定为显示第 2 列。
10. 0.2.2 补上 Waveform 自身布局的持久化。主线程观察实际 Header 顺序/宽度，变化后防抖 250ms 保存 `wave_order` 和 `wave_width`；移除列或退出时立即保存。启动读取并按保存的显示位置插入，不再每次强制回到 File 之后。
11. 0.2.2 对现有峰值先校验来源文件大小/修改时间及 REAPER 报告的 `.reapeaks` 修改时间，有效时直接 `PCM_Source_GetPeaks`，避免先进入 builder。批处理目标调整为 8ms/64 步，不保证单次解码器调用耗时。源音频变化时，REAPER 可能仍复用旧峰值对象；先调用公开 `ClearPeakCache` 清除全局内存峰值引用，并只使该源文件对应的派生 `.reapeaks` 失效，再通过原有构建 API 重建。扩展内其他已缓存的峰值数组保留。

用户明确选择不新增波形磁盘缓存，因此没有在扩展中持久保存峰值数组或位图。跨进程必须重新读取现有 REAPER 峰值和绘制位图；优化目标是避免不必要的构建和逐文件等待，不能声称重启后零 I/O 或零绘图。

## 0.3.0：绕过音频解码源，直接读取缓存并整页发布

0.2.2 虽然能跳过峰值构建，仍逐文件创建 PCM_source 并调用源峰值 API；8ms 时间片后让出主线程，也没有整页发布机制。因此“未重建峰值”不能证明首帧已完整或不会逐个出现。

0.3.0 按 [Cockos 官方格式](https://www.reaper.fm/sdk/reapeaks.txt) 实现只读解析器，支持 RPKM、RPKN、RPKL 主波形层，按时间区间聚合各声道最大/最小值，保留 192 点包络。跳过末尾频谱/响度层；允许短文件粗层计数为零。优先使用足够分辨率的较粗层，并从最细层补齐粗层未覆盖的末尾，避免遗漏末尾瞬态。短音频只使用缓存已有分辨率，不为增加细节打开音频解码器。

两个后台线程只调用文件读取和纯数据解析，不调用任何 REAPER API 或 GDI。主线程提供经过 REAPER 路径 API 定位的峰值文件名、源文件属性、修订号和任务编号；结果回到主线程后再次验证源属性及任务身份。切换视图时取消尚未开始的离屏任务，过期结果不会覆盖新文件数据。卸载时等待线程退出。

主线程等当前页的缓存读取批次全部就绪后才发布新波形，再统一触发列表重绘。诊断新增 `first_draw_ready`（首次真正绘制波形时已发布的行数）、`page_publications`、`page_ready_ms`、`direct_peak_reads`。它们覆盖实际绘制回调，而不只统计数据或 builder 状态。

解析器校验文件头、版本、层数、源大小/时间、区间范围、整数溢出、选中数据大小（上限 32 MiB）、读前读后文件属性和上下峰值有效性。缺失、过期、损坏或不支持的缓存回退 REAPER 主线程路径。已有缓存的备用读取也会等待同批缓存素材准备好；确实进入重建的文件不阻塞其他已准备好的波形。继续不创建额外的磁盘缩略图或峰值数据库。

## 0.3.1：降低已有素材显示时的 CPU 工作量

原版即使读取已缓存，每秒仍对整页执行文本查询、路径解析和重复属性查询，每 300ms 重新序列化状态；位图未命中时每个横向像素调用 MoveToEx/LineTo。缓存数据不变也会周期性遍历已完成的队列。

新版本保留原生数据变化、目录、视口和排序事件驱动的行解析；空闲时只校验已知完整路径的属性，默认每秒一次。解析阶段复用已取得的属性，跳过错误的当前工作目录探测，文本缓冲不再整块清零。每页存储稳定的 Wave 引用供绘制访问，位图键不再复制路径。处理已完成后停止队列遍历，静态页面的轻量界面检查节流至约 100ms，布局检查为 500ms；变化的文件仍会失效并重新读取。

位图通过 CreateDIBSection 后直接填像素，先为 192 个峰值计算上下坐标，再填充相应像素列，随后一次 BitBlt。保留旧 GDI 实现仅作创建失败时的回退。背景填充使用 DC_BRUSH，数据刷新只使波形列或变化的单元格重画，原生的其他列无需被本扩展额外重画。位图个数上限扩为 512，16 MiB 像素预算不变；峰值数组仍不按条数淘汰。

后台文件读取线程降为 below-normal 优先级；没有完成结果时主线程避免获取结果队列锁。诊断内容仅在状态/绘制改变时更新。性能探针和基准专用命令受 `MWT_PROFILE` 编译开关控制，正式构建不包含 QueryThreadCycleTime/Scoped QPC 探针。

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
