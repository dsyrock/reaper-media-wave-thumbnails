# Media Explorer 波形缩略图

当前正式版：**v0.4.2**，Windows、macOS、Linux 插件内部版本均为 0.4.2。支持点击波形相对位置进行原生试听。Windows 已通过定位及回归测试；Mac/Linux 已通过云端检查，用户也已反馈实机测试通过。操作和验证范围见 [docs/CLICK-PREVIEW.md](docs/CLICK-PREVIEW.md)。

源码和自动构建位于 [GitHub 仓库](https://github.com/dsyrock/reaper-media-wave-thumbnails)。安装包请从 [Release v0.4.2](https://github.com/dsyrock/reaper-media-wave-thumbnails/releases/tag/v0.4.2) 下载，按系统和架构选择 ZIP；公开 Release 附件无需登录 GitHub。

Linux x86_64 已通过云端编译、核心和模拟宿主接口测试，并获用户实机验证反馈；安装说明见 [docs/LINUX.md](docs/LINUX.md)。macOS 说明见 [docs/MACOS.md](docs/MACOS.md)。

在 REAPER 原生 Media Explorer 的 Details（详细信息）列表中增加 `Waveform` 列，无需逐个点击音频即可看到整段波形。Windows 点击试听已在 REAPER 7.84 验证，既有波形功能此前在 6.83/7.84 验证。

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
- 单击波形的相对位置进行原生定位试听；点击时下方预览恢复全长显示。拖拽和修饰键选择保留，波形双击不插入工程。详见 [点击试听说明](docs/CLICK-PREVIEW.md)。
- 排除 MusicXML（xml/musicxml/mxl）、MIDI 和工程/文本格式，不为浏览缩略图自动打开这些文件，避免触发乐谱导入器及其错误弹窗；手动预览/导入仍由 REAPER 处理。
- 文件夹、数据库和搜索列表共用原生列表及选择行为。
- 保留原生列的顺序、宽度和隐藏状态。Temporary Mark（表头为 `Mark`）、Fav 等可调整位置；Waveform 自身的位置与宽度单独保存在 REAPER ExtState，支持放在第一栏并跨重启保留。原生应用列布局、重建列或退出保存布局时，暂时移除本扩展列，避免列数冲突。
- 有效的现有 `.reapeaks` 由两个后台读取线程直接解析，不创建音频解码源、不调用源峰值读取 API。采用 [Cockos 公开格式](https://www.reaper.fm/sdk/reapeaks.txt)，支持 RPKM/RPKN/RPKL 波形层；优先读合适分辨率，末尾不足一个粗层区间时从细层补齐。
- 当前页的缓存读取准备好后统一发布波形，避免每读完一个文件就画出一个。视图稳定等待约 80ms，以减少快速切换目录/滚动产生的无效读取。已经在内存中的已显示波形继续直接复用。
- 没有缓存、缓存过期或格式无法直接读取时，回退到 REAPER 原有峰值读取/构建流程。真正需要构建的新文件仍可能逐渐出现。文件变更时保留对过期缓存的失效处理。
- 以完整路径、大小和修改时间区分缓存。已访问文件的峰值数组在本次运行中保留，不再按 512 个淘汰，也不会因等待一段时间自动过期。切换目录、关闭/打开功能均保留峰值；手动刷新缓存或退出 REAPER 时清空。
- 缓存命中的可见行一起恢复；目录切换和滚动触发尽快更新，不再等待原来的 200ms 周期。仍需读取的峰值按短时间预算批量处理。
- 近期显示的波形缓存为位图，普通重绘直接贴图；位图缓存最多 512 张、估算像素大小不超过 16 MiB。宽高、颜色或文件内容变化时生成对应的新位图，淘汰位图不会删除峰值数组。
- 0.3.1 使用直接写入 DIB 像素的方式生成位图，避免每个横向像素各调用一次 GDI 绘图。位图键使用文件修订号，绘制时使用已解析的行引用，减少路径字符串复制和重复查找；输出 RGB 像素与旧绘制方式一致。
- 空闲时不再重复解析整页原生文本，不反复遍历已完成的读取队列，也不定时重写未变化的诊断 JSON；仍每秒校验可见文件属性，检测文件更新。波形数据变化仅使波形列/对应单元格重绘。后台缓存读取线程使用较低的调度优先级。
- 峰值数组本身每个文件约 1.5 KiB，另有路径和容器开销，内存会随已访问文件数量增长。本版不增加额外的磁盘缩略图缓存；重启后继续通过 REAPER 读取已有 `.reapeaks`。
- 重启仍需要读取峰值并生成本次进程的位图；没有承诺跨重启零读取或零绘图。只保存列布局设置，不保存额外的波形库或位图文件。
- 各缩略图按自身峰值缩放，便于观察较轻的声音；静音显示为中线，损坏或不支持的文件留空。
- 为解析隐藏路径的数据库/搜索行，必要时同步切换完整路径显示，读取后立即恢复完整路径及前导路径选项。
- 0.3.2 在原生行绘制完成后覆盖波形，不再强制向原生主题代码发送额外的子项绘制通知；波形绘制保存/恢复 DC 并限制到本列。新页异步数据首次就绪时补做一次原生区域刷新，后续波形更新仍尽量局部重画。

本扩展不改写 `reaper.exe` 或 `Plugins/reaper_explorer.dll`。已有缓存按公开格式只读解析，回退处理使用公开 C API；列表控件编号、列序及绘制通知属于实现依赖。不同 REAPER 版本、第三方列表绘制扩展、全部排序/表头菜单组合及大规模素材库性能仍需分别回归。

## 构建与验证

需要 Windows、MinGW-w64 `g++`（本次为 15.2）。运行 `./build.ps1`。DLL 静态链接 GCC、C++ 与 pthread 运行库，仅依赖 Windows 系统 DLL。

开发基准可用 `./build.ps1 -CpuProfile` 后运行 `tests/cpu-benchmark.lua`。普通构建不编入计时探针及测试用的位图清除命令，发布包采用普通构建。

源码不调用 `PCM_source` 的 C++ 虚函数，避免 MinGW 与 MSVC 的 C++ ABI 不兼容。Cockos SDK 头文件原样保留在 `third_party/reaper/`，许可见文件头。

研究边界、REA 证据及实测结果见 [docs/RESEARCH.md](docs/RESEARCH.md) 和 [docs/VALIDATION.md](docs/VALIDATION.md)。
