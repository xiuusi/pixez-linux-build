# PixEz Flutter Linux 客户端构建方案

这是 [PixEz-Flutter](https://github.com/Notsfsssf/pixez-flutter) 的 Linux 平台构建支持文件。

> **注意：本文件由 AI（~~哈基米网页免费版&Claude网页免费版~~由glm5.2接手）生成和维护。** 

---

## ⚠️ 注意事项

本构建仅补齐 Linux 桌面端的 **原生能力**（窗口、保存等），并不改写主仓库 `lib/` 下的 Dart 业务代码，因此以下限制源自上游，本项目无法绕过：

- **触屏操作逻辑**：界面交互仍沿用上游的移动端手势（长按、滑动、缩放等），未针对键鼠做适配。桌面端的滑动、滚轮懒加载等部分行为已通过上游既有的 `Platform.isLinux` 判断生效，但整体体验并非桌面原生。
- **复制图片功能**：原生 channel 已实现 GTK 剪贴板写入，但受上游 Dart 侧调用链限制，部分场景下复制可能失效，属于已知缺陷。
- **AI风险**：本项目为AI维护，可能存在大量未发现问题，请谨慎使用。

---

## 构建方法

### 前置要求

确保已安装以下系统依赖：

```bash
# Debian/Ubuntu
sudo apt-get install -y clang cmake ninja-build pkg-config libgtk-3-dev liblzma-dev

# Fedora
sudo dnf install -y clang cmake ninja-build pkg-config gtk3-devel xz-devel

# Arch Linux
sudo pacman -S --needed clang cmake ninja pkgconf gtk3
```

### Flutter 环境

- [Flutter SDK](https://flutter.dev/docs/get-started/install/linux) 3.0 或更高版本
- 验证环境：
  ```bash
  flutter doctor
  ```

### 构建步骤

1. **下载仓库中的 `linux` 文件夹，复制到你 git 的 pixez-flutter 目录**

2. **获取依赖并构建**

```bash
flutter pub get

cd plugins/rhttp
flutter pub get
dart run build_runner build --delete-conflicting-outputs
cd ../..

dart run build_runner build --delete-conflicting-outputs

flutter pub get
flutter run -d linux
```

### 构建发布版

```bash
flutter build linux --release
```

构建输出位于 `build/linux/x64/release/bundle/` 目录。

---

## 下载图片保存位置

应用下载的图片默认保存在：

```
~/Pictures/pixez/
```

---

## 相关链接

- [PixEz-Flutter 主仓库](https://github.com/Notsfsssf/pixez-flutter)
- [PixEz 官方下载](https://github.com/Notsfsssf/pixez-flutter/releases)

---

