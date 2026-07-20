# PixEz Linux Build

[PixEz-Flutter](https://github.com/Notsfsssf/pixez-flutter) 的 Linux 平台原生支持层。

本仓库提供 PixEz 在 Linux 桌面端所需的原生能力——窗口管理、文件保存、剪贴板、单实例、GIF 编码等。覆盖 `linux/` 目录，**不修改主线 Dart 业务代码**。

## 插件清单

| 插件 | 通道 | 功能 |
|------|------|------|
| `DocumentPlugin` | `com.perol.dev/save` | 文件保存、另存为对话框、路径读写、权限查询 |
| `ClipboardPlugin` | `com.perol.dev/clipboard` | 剪贴板读写（GTK 实现） |
| `SafPlugin` | `com.perol.dev/saf` | SAF 兼容层，目录选择与文件写入 |
| `WeissPlugin` | `com.perol.dev/weiss` | 辅助功能插件 |
| `PathsPlugin` | `com.perol.dev/paths` | 系统路径查询 |
| `SingleInstancePlugin` | `com.perol.dev/single_instance` | 单实例检测（Unix socket） |
| `EncodePlugin` | `samples.flutter.dev/battery` | **Ugoira 动图 GIF 编码**（giflib + GdkPixbuf） |

## 前置依赖

```bash
# Arch Linux
sudo pacman -S --needed clang cmake ninja pkgconf gtk3 giflib

# Debian / Ubuntu
sudo apt-get install -y clang cmake ninja-build pkg-config libgtk-3-dev libgif-dev

# Fedora
sudo dnf install -y clang cmake ninja-build pkg-config gtk3-devel giflib-devel
```

此外需要 [Flutter SDK](https://flutter.dev/docs/get-started/install/linux) 3.0 或更高版本。

## 构建步骤

```bash
# 1. 克隆主线
git clone https://github.com/Notsfsssf/pixez-flutter.git
cd pixez-flutter

# 2. 获取 linux-build 的 linux/ 目录
git clone https://github.com/Here-is-Daiyu/pixez-linux-build.git
cp -a pixez-linux-build/linux .

# 3. 获取 Dart 依赖
flutter pub get
cd plugins/rhttp && flutter pub get && cd ../..
dart run build_runner build --delete-conflicting-outputs
flutter pub get

# 4. 构建
flutter build linux --release
```

构建产物位于 `build/linux/x64/release/bundle/`。

## 已知限制

- **触屏交互**：仍沿袭上游的移动端手势设计（长按、滑动、缩放等），未针对键鼠做专项适配。
- **复制图片**：ClipboardPlugin 原生通道已实现 GTK 剪贴板写入，但上游 Dart 层 `lib/clipboard_plugin.dart` 仅在 `Platform.isWindows` 时走插件路径，Linux 端暂不走通，实测复制后剪贴板无内容。

## 保存位置

应用下载的图片默认保存在：

```
~/Pictures/PixEz/
```

可在设置页面中修改保存路径。

## 相关链接

- [PixEz-Flutter 主仓库](https://github.com/Notsfsssf/pixez-flutter)
- [PixEz 官方 Releases](https://github.com/Notsfsssf/pixez-flutter/releases)
- [Arch Linux 打包 (pixez-flutter-bin)](https://github.com/Here-is-Daiyu/pixez) — 本构建的预编译包
