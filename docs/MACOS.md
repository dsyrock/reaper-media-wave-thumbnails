# macOS 适配（实验性源码，尚未在 Mac 编译或实测）

此阶段提供 macOS 后端和构建入口，不表示已产生可验证的 Mac 插件。仓库由用户稍后提供，当前没有推送代码、创建远程仓库或触发 Actions。

## 已实现

- 共享 `platform_file.hpp`：Windows Win32 和 macOS/POSIX 的只读文件、随机读取、源文件属性和读前/读后校验。内部修改时间统一为 1601 起 100ns ticks，维持原有 reapeaks 校验语义；POSIX 路径按 UTF-8 转换，不依赖系统 locale。
- 峰值解析、后台读取队列、分辨率选择和末尾瞬态处理共用原代码。后台线程不操作 AppKit 或 REAPER API。
- `wave_geometry.hpp` 共用几何；Windows 使用原有 DIB，Mac 使用透明 NSBitmapImageRep/NSImage，按 backingScaleFactor 生成 Retina 像素。
- Mac 插件通过 host-provided SWELL 定位已存在的 Media Explorer（1001 列表 / 1002 路径），使用官方 SWELL 列插入和删除，波形 NSCell 独立绘制，不替换原生表格类。
- 自有列通知过滤、原生布局菜单前临时撤列、原生列逻辑/显示顺序映射、独立 `mac_wave_order` / `mac_wave_width` 设置、开关与刷新动作。
- `.reapeaks` 直接读取，源音频读取/峰值构建回退在主线程进行。没有额外的峰值磁盘数据库。

依据的 SWELL 源码固定为 `03c6e564b881a41cb13441cfd7350e92ae1ddf4d`。重要差异：Mac 的 custom draw 来自 Cocoa `willDisplayCell`，没有 Windows 行后绘制 HDC；其 GetColumnOrderArray 为逻辑列到显示位置的映射。不能复用 Windows 的 subclass / GDI 实现。

## 构建

需要 macOS 11+、Xcode Command Line Tools、CMake 3.24+、Git：

```sh
bash build-macos.sh arm64       # Apple Silicon
bash build-macos.sh x86_64      # Intel
bash build-macos.sh universal  # 同时包含两种架构
```

CMake 会获取固定版本 WDL，只编译 `swell-modstub.mm`（从正在运行的 REAPER 获取 SWELL API），不将另一份 SWELL 控件实现嵌入宿主。也可传入 `-DMWT_WDL_DIR=/path/to/WDL` 使用已有 checkout。SDK 原文件不修改，构建时暂存到正确的相对目录。

输出是 `dist/macos-<架构>/reaper_media_wave_thumbnails.dylib`。脚本使用 ad-hoc 签名，不是 Apple Developer ID 签名或公证。正常退出 Mac REAPER 后安装到其资源目录 `UserPlugins`；资源目录以 REAPER 的“显示资源目录”菜单为准。

## GitHub Actions

`.github/workflows/build.yml` 已准备：

- `macos-15` 构建 arm64；`macos-15-intel` 构建 x86_64。
- 执行 CTest 核心测试、架构检查、入口符号检查，再 ad-hoc 签名并上传 experimental 构建产物。
- Ubuntu 验证 POSIX 共享层；Windows 编译原有扩展并运行共享层测试。

上述工作流当前**未在 GitHub 执行**。只有 Actions 真正通过后，才可说 Mac 编译已验证。它也不能代替 REAPER 内的 UI 功能验证。

## 必须在 Mac 中验证的内容

1. DLL 等价的 dylib 正常加载，`tests/mac-smoke.lua` 报告正确平台与附件状态。
2. 控件类型为预期的 SWELL NSCell-based NSTableView；view-based 或无窗口过程的实现会拒绝挂接并报告诊断。
3. 文件夹/数据库/搜索、中文文件名、同名不同路径、缺失/过期峰值、源文件变更。
4. Retina 与普通显示器、深浅主题、选择颜色，原生列配置/排序/拖拽，列顺序和宽度重启保持。
5. 关闭窗口/停靠切换/退出的清理行为。其他扩展同时替换同一 SWELL 窗口过程的情况，以及动态卸载尚未验证，不作为支持承诺。

目前 Mac 后端的 UI 轮询/原生列保护尚未达到 Windows 实机验证范围；其行为和性能不能直接套用 Windows 的测量结果。

## 本地验证记录

Windows：CMake/MinGW 插件构建通过；CTest `portable_core` 通过（Unicode 路径、属性和随机读取、立体声/末尾瞬态、队列、损坏数据）；原读取器自测和旧 GDI 像素对比通过。按 reaper-test 通过了原波形/缓存/坏文件/同名文件及源文件变化回归。

Mac：无本地 SDK/机器，未编译、未加载；POSIX 分支等待 Linux/macOS 云端测试。没有将 Windows 测试结果冒充 Mac 验证。

主要参考：[Cockos SWELL](https://github.com/justinfrankel/WDL/tree/main/WDL/swell)、[Apple NSTableView](https://developer.apple.com/documentation/appkit/nstableview)、[Cockos 扩展 SDK](https://www.reaper.fm/sdk/plugin/plugin.php)。
