# Media Explorer 波形缩略图

在 REAPER 原生 Media Explorer 的 Details（详细信息）列表中增加 `Waveform` 列，无需逐个点击音频即可看到整段波形。当前为 Windows x64 实验版 0.2.2，已在 REAPER 7.84 实测。

## 使用

1. 正常退出目标目录的 REAPER。
2. 执行 `./install.ps1 -ReaperRoot 'K:\REAPER-test'`，或将编译的 DLL 放到该安装的 `UserPlugins`。
3. 重新启动 REAPER，打开 Media Explorer 并使用 Details 视图。没有保存过设置时，波形列默认出现在文件名右侧；调整位置和宽度后会保存，重启继续使用。旧版本未保存过波形列的位置，更新后请按偏好调整一次。

主动作列表中搜索 `waveform thumbnails`：

- `Media Explorer: Toggle waveform thumbnails`：开关波形列；关闭时移除本扩展的列及窗口挂接。
- `Media Explorer: Refresh waveform thumbnail cache`：重新读取波形。

卸载：退出 REAPER 后删除 `UserPlugins/reaper_media_wave_thumbnails.dll`。

## 行为

- 优先为可见行生成波形，滚动后继续生成；无需先扫描整个素材库。
- 文件夹、数据库和搜索列表共用原生列表及选择行为。
- 保留原生列的顺序、宽度和隐藏状态。Temporary Mark（表头为 `Mark`）、Fav 等可调整位置；Waveform 自身的位置与宽度单独保存在 REAPER ExtState，支持放在第一栏并跨重启保留。原生应用列布局、重建列或退出保存布局时，暂时移除本扩展列，避免列数冲突。
- 使用 REAPER 解码器和峰值缓存；首次读取可能生成 REAPER 自己的 `.reapeaks` 文件。
- 重启后优先直接读取有效的现有 `.reapeaks`，读取成功的文件不进入峰值构建流程，按目标 8ms/最多 64 个步骤成批处理。文件变更或峰值文件过期时才重新构建；会使该音频对应的过期 `.reapeaks` 失效并调用 REAPER 的峰值缓存清理 API。
- 以完整路径、大小和修改时间区分缓存。已访问文件的峰值数组在本次运行中保留，不再按 512 个淘汰，也不会因等待一段时间自动过期。切换目录、关闭/打开功能均保留峰值；手动刷新缓存或退出 REAPER 时清空。
- 缓存命中的可见行一起恢复；目录切换和滚动触发尽快更新，不再等待原来的 200ms 周期。仍需读取的峰值按短时间预算批量处理。
- 近期显示的波形缓存为位图，普通重绘直接贴图；位图缓存最多 256 张、估算像素大小不超过 16 MiB。宽高、颜色或文件内容变化时生成对应的新位图，淘汰位图不会删除峰值数组。
- 峰值数组本身每个文件约 1.5 KiB，另有路径和容器开销，内存会随已访问文件数量增长。本版不增加额外的磁盘缩略图缓存；重启后继续通过 REAPER 读取已有 `.reapeaks`。
- 重启仍需要读取峰值并生成本次进程的位图；没有承诺跨重启零读取或零绘图。只保存列布局设置，不保存额外的波形库或位图文件。
- 各缩略图按自身峰值缩放，便于观察较轻的声音；静音显示为中线，损坏或不支持的文件留空。
- 为解析隐藏路径的数据库/搜索行，必要时同步切换完整路径显示，读取后立即恢复完整路径及前导路径选项。

本扩展不改写 `reaper.exe` 或 `Plugins/reaper_explorer.dll`。音频处理使用公开 C API；列表控件编号、列序及 Windows 绘制通知属于实现依赖，尚未验证其他 REAPER 版本、非 Windows 系统及第三方列表绘制扩展的组合。试听声音、鼠标拖拽、所有排序/表头菜单组合及大规模素材库性能尚未全面测试。

## 构建与验证

需要 Windows、MinGW-w64 `g++`（本次为 15.2）。运行 `./build.ps1`。DLL 静态链接 GCC、C++ 与 pthread 运行库，仅依赖 Windows 系统 DLL。

源码不调用 `PCM_source` 的 C++ 虚函数，避免 MinGW 与 MSVC 的 C++ ABI 不兼容。Cockos SDK 头文件原样保留在 `third_party/reaper/`，许可见文件头。

研究边界、REA 证据及实测结果见 [docs/RESEARCH.md](docs/RESEARCH.md) 和 [docs/VALIDATION.md](docs/VALIDATION.md)。
