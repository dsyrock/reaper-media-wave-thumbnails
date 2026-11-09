# Linux 适配（实验性）

Linux 后端在 REAPER 原生 Media Explorer 的详细列表中插入 Waveform 列，输出 `reaper_media_wave_thumbnails.so`。当前目标为 Linux x86_64；其他 CPU 架构尚未验证。

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

GitHub Actions 使用 Ubuntu 22.04 x86_64，产物名为 `linux-x86_64-experimental`。这是原生 Linux 插件，正常退出 REAPER 后，将 `.so` 放入 REAPER 资源目录的 `UserPlugins`。资源目录请以 REAPER 的“显示资源目录”菜单为准。安装后在主动作列表搜索 `waveform thumbnails`。

## 验证范围

- `portable_core` 检查 Unicode 文件路径、源属性、随机读取、峰值解析、立体声末尾瞬态、绘图几何、后台队列和损坏缓存。
- `linux_host` 用 `dlopen` 加载真实插件产物，通过模拟 REAPER/SWELL 接口检查插件加载/卸载、原生通知转发、有效 reapeaks 不解码音频、透明位图复用、布局保存与开关、原生列设置隔离、源文件变更和 SDK 回退。
- 模拟宿主测试不启动实际 REAPER，也不运行真实 SWELL 控件/绘图实现，不能替代 UI 实测。
- 本机是 Windows，没有 Linux REAPER 测试环境。实际加载、显示、试听、滚动、深浅主题、缩放、停靠窗口和重启布局仍需在 Linux REAPER 验证；`tests/linux-smoke.lua` 提供实际挂接检查入口。

2026-11-09，[构建 42645521554](https://github.com/dsyrock/reaper-media-wave-thumbnails/actions/runs/42645521554) 全部通过，对应源码提交 `3f73d10c29d74336cc6102cab32a5f8c0f6ffc75`：

- Linux 使用 GCC 11.4.0，`portable_core` 和 `linux_host` 两项 CTest 均通过；ELF x86_64 架构、`ReaperPluginEntry` / `SWELL_dllMain` 导出和 `ldd -r` 检查通过。
- Mac arm64/x86_64 和 Windows x64 同一提交的构建及核心测试通过。
- Linux `.so` 的 SHA-256：`aac4fe589540ffa9687461abd60612741aca960b31ab18cde32b4ecd5bae20dc`。

在上述构建页面的 Artifacts 下载 `linux-x86_64-experimental`。产物动态依赖系统 libc、libstdc++ 等运行库；较老发行版如无法加载，需在目标发行版重新构建。没有链接 GTK 或独立 SWELL 实现。

修正记录：首次编译使用了 SWELL 不提供的 Win32 选中颜色常量，已改为使用通知中的原生选中文字颜色；宿主测试最初拖错目标列，修正为按逻辑列定位后，全轮测试通过。编译日志仍有上游 SWELL 的未使用参数提示和 Actions 依赖弃用提示，不代表实际 UI 已验证。

参考：[Cockos SWELL generic 窗口实现](https://github.com/justinfrankel/WDL/blob/03c6e564b881a41cb13441cfd7350e92ae1ddf4d/WDL/swell/swell-wnd-generic.cpp)、[generic 绘图实现](https://github.com/justinfrankel/WDL/blob/03c6e564b881a41cb13441cfd7350e92ae1ddf4d/WDL/swell/swell-gdi-lice.cpp)。
