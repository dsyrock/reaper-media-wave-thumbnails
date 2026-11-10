# macOS 适配

当前正式版本为 0.4.2。公开仓库为 [dsyrock/reaper-media-wave-thumbnails](https://github.com/dsyrock/reaper-media-wave-thumbnails)，Mac arm64 与 x86_64 插件均已通过云端构建和核心测试，用户于 2026-11-10 反馈 Mac 实机测试通过。安装包见 [Release v0.4.2](https://github.com/dsyrock/reaper-media-wave-thumbnails/releases/tag/v0.4.2)。以下旧版构建记录保留当时的验证边界。

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

2026-11-09，[构建 42626370323](https://github.com/dsyrock/reaper-media-wave-thumbnails/actions/runs/42626370323) 全部通过，对应源码提交 `d4d1d512a6361b994a9756f175dbad9f5d7c6261`：

| 任务 | 结果与产物 |
| --- | --- |
| macOS arm64 | 编译、CTest、Mach-O 架构及入口校验、ad-hoc 签名通过；`macos-arm64-experimental` |
| macOS x86_64 | 同上；`macos-x86_64-experimental` |
| Windows x64 / MSVC | 编译、CTest 通过；`windows-x64` |
| Ubuntu | POSIX 共享代码编译、CTest 通过 |

下载时打开上述构建页面，在 Artifacts 中选择自己的架构，解压得到 `reaper_media_wave_thumbnails.dylib`。需要登录 GitHub。云端构建不能代替 REAPER 内的 UI 功能验证。

## 后续 Mac 实机回归关注点

1. DLL 等价的 dylib 正常加载，`tests/mac-smoke.lua` 报告正确平台与附件状态。
2. 控件类型为预期的 SWELL NSCell-based NSTableView；view-based 或无窗口过程的实现会拒绝挂接并报告诊断。
3. 文件夹/数据库/搜索、中文文件名、同名不同路径、缺失/过期峰值、源文件变更。
4. Retina 与普通显示器、深浅主题、选择颜色，原生列配置/排序/拖拽，列顺序和宽度重启保持。
5. 关闭窗口/停靠切换/退出的清理行为。其他扩展同时替换同一 SWELL 窗口过程的情况，以及动态卸载尚未验证，不作为支持承诺。

Mac 实机结果来自用户反馈；下列 Windows 性能测量不能直接作为 Mac 的测量结果。

## 本地验证记录

Windows：CMake/MinGW 插件构建通过；CTest `portable_core` 通过（Unicode 路径、属性和随机读取、立体声/末尾瞬态、队列、损坏数据）；原读取器自测和旧 GDI 像素对比通过。按 reaper-test 通过了原波形/缓存/坏文件/同名文件及源文件变化回归。

云端：Mac 两种架构已实际编译并运行核心测试；Linux POSIX 测试与 Windows MSVC 构建也已通过。首次构建的 `lipo -verify_arch` 参数顺序有误，修正后完整重跑通过。2026-11-10 用户补充反馈 Mac 实机测试通过。

主要参考：[Cockos SWELL](https://github.com/justinfrankel/WDL/tree/main/WDL/swell)、[Apple NSTableView](https://developer.apple.com/documentation/appkit/nstableview)、[Cockos 扩展 SDK](https://www.reaper.fm/sdk/plugin/plugin.php)。
