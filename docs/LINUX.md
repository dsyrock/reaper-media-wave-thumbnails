# Linux 适配

Linux 后端在 REAPER 原生 Media Explorer 的详细列表中插入 Waveform 列，输出 `reaper_media_wave_thumbnails.so`。当前目标为 Linux x86_64；其他 CPU 架构尚未验证。

当前正式版本为 0.4.2。用户于 2026-11-10 反馈 Linux 实机测试通过；自动化构建及新旧 SWELL 模拟宿主测试也已通过。正式安装包见 [Release v0.4.2](https://github.com/dsyrock/reaper-media-wave-thumbnails/releases/tag/v0.4.2)，Actions 产物名为 `linux-x86_64`。以下旧版记录保留各次测试当时的验证边界。

## 实现与边界

- 通过 REAPER 提供的 SWELL 函数表工作；构建只包含官方 `swell-modstub-generic.cpp`，不嵌入另一份窗口系统，也不链接构建机器的 `libSwell.so`。
- Linux 的 generic SWELL 每个单元格发出 `CDDS_ITEMPREPAINT`。只处理自己的逻辑列，原生列的文本、颜色和通知仍交给 REAPER。
- 缓存透明 LICE 位图，通过 host SWELL alpha blit 绘图，不修改共享 DC 的颜色、画笔或裁剪栈。峰值数组在会话中保留，位图限制 512 张/约 16 MiB。
- 共用原有 reapeaks 解析、POSIX 文件读取及后台线程；没有增加磁盘缩略图库。有效峰值在后台读取，缺失/过期时在主线程调用 REAPER 的峰值构建接口。
- Waveform 的位置和宽度以 `linux_wave_order`、`linux_wave_width` 单独保存。原生列设置命令和退出前临时移除自有列。
- 沿用当前 REAPER 控件编号和原生设置命令，属于实现依赖；其他版本、其他扩展同时改写该窗口过程、所有菜单/排序/拖拽组合尚未验证。仅支持正常退出后更新插件，不承诺热卸载兼容性。

## 构建与安装

需要 GCC/G++、CMake 3.24+、Git 和 pthread。运行：

```sh
bash build-linux.sh
```

输出 `dist/linux/reaper_media_wave_thumbnails.so`。CMake 会拉取固定 WDL 提交；离线构建可自行预备 WDL 并传 `-DMWT_WDL_DIR=/path/to/WDL`。也可用 `-DMWT_BUILD_PLUGIN=OFF` 只测试共享代码。

GitHub Actions 使用 Ubuntu 22.04 x86_64，产物名为 `linux-x86_64`。这是原生 Linux 插件，正常退出 REAPER 后，将 `.so` 放入 REAPER 资源目录的 `UserPlugins`。资源目录请以 REAPER 的“显示资源目录”菜单为准。安装后在主动作列表搜索 `waveform thumbnails`。

## 验证范围

- `portable_core` 检查 Unicode 文件路径、源属性、随机读取、峰值解析、立体声末尾瞬态、绘图几何、后台队列和损坏缓存。
- `linux_host` 用 `dlopen` 加载真实插件产物，通过模拟 REAPER/SWELL 接口检查插件加载/卸载、原生通知转发、有效 reapeaks 不解码音频、透明位图复用、布局保存与开关、原生列设置隔离、源文件变更和 SDK 回退。
- 模拟宿主测试不启动实际 REAPER，也不运行真实 SWELL 控件/绘图实现，不能替代 UI 实测。
- 本机是 Windows；Linux 实机结果来自用户反馈。`tests/linux-smoke.lua` 提供后续实际挂接回归入口。

## 0.4.1：XML 自动导入与旧版重复建列修复

2026-11-10，[构建 42724090986](https://github.com/dsyrock/reaper-media-wave-thumbnails/actions/runs/42724090986) 全部通过，对应源码提交 `88fc968fcc26f222029d501e059452f9d06865db`。

- `IsMediaExtension` 包含 MIDI/MusicXML，不能作为“安全生成音频波形”的唯一条件。三个后端都在解析文件名及调用源创建前排除 xml/musicxml/mxl、MIDI、工程和文本格式，正常/异常 XML 都不进入波形队列。
- 旧版 SWELL 尚无 `ListView_GetColumn`：[官方引入提交](https://github.com/justinfrankel/WDL/commit/826d93fbe480483b4bd8e94d0f7bba7619db1382) 日期为 2023-12-28，晚于 REAPER 6.83。原实现读取不到表头就认为列丢失。现在用窗口属性记录自身插入的列，并校验逻辑索引和列数，不再调用该新接口；遇到无法确认的原生列变化时暂停，不盲目补列。
- Linux 的 `portable_core`、`linux_host`、`linux_host_legacy` 三项 CTest 全部通过。旧接口测试令 `ListView_GetColumn` 缺失，连续扫描仍只插入一次；混合 WAV、正常/异常 XML、MIDI 时，非音频源创建调用为零；额外原生列造成的所有权不确定不会触发增列。
- Windows 6.83/7.84 的独立 REAPER 实测：报告提供的两个 XML 均在原生列表中可见，只有 WAV 进入波形缓存；列数分别稳定为 25/26。原有音频/缓存/开关回归均通过。此项为 Windows 实测，不能冒充 Linux 6.83 实测。
- 新 Linux `.so` SHA-256：`d343b06d59ce7862e37395ca0755d6479ea8d75e588b5224d2fd92594f49a0e7`。

更新时正常退出 REAPER，再替换 UserPlugins 中的 `.so`；旧版已经增出的临时列随窗口销毁清除。Linux 6.83 的真实界面仍需用户验证。

## 首次 Linux 适配记录

2026-11-09，[构建 42645521554](https://github.com/dsyrock/reaper-media-wave-thumbnails/actions/runs/42645521554) 全部通过，对应源码提交 `3f73d10c29d74336cc6102cab32a5f8c0f6ffc75`：

- Linux 使用 GCC 11.4.0，`portable_core` 和 `linux_host` 两项 CTest 均通过；ELF x86_64 架构、`ReaperPluginEntry` / `SWELL_dllMain` 导出和 `ldd -r` 检查通过。
- Mac arm64/x86_64 和 Windows x64 同一提交的构建及核心测试通过。
- Linux `.so` 的 SHA-256：`aac4fe589540ffa9687461abd60612741aca960b31ab18cde32b4ecd5bae20dc`。

在上述构建页面的 Artifacts 下载 `linux-x86_64-experimental`。产物动态依赖系统 libc、libstdc++ 等运行库；较老发行版如无法加载，需在目标发行版重新构建。没有链接 GTK 或独立 SWELL 实现。

修正记录：首次编译使用了 SWELL 不提供的 Win32 选中颜色常量，已改为使用通知中的原生选中文字颜色；宿主测试最初拖错目标列，修正为按逻辑列定位后，全轮测试通过。编译日志仍有上游 SWELL 的未使用参数提示和 Actions 依赖弃用提示，不代表实际 UI 已验证。

参考：[Cockos SWELL generic 窗口实现](https://github.com/justinfrankel/WDL/blob/03c6e564b881a41cb13441cfd7350e92ae1ddf4d/WDL/swell/swell-wnd-generic.cpp)、[generic 绘图实现](https://github.com/justinfrankel/WDL/blob/03c6e564b881a41cb13441cfd7350e92ae1ddf4d/WDL/swell/swell-gdi-lice.cpp)。
