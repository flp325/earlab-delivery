# EarLab 0.6.1 macOS 本地构建包

在 Mac 的 Finder 中解压整个 ZIP，打开 EarLab-0.6.1 文件夹，双击 **双击生成 DMG.command**。构建期间请保持终端窗口打开。成功后 Finder 会选中生成的 DMG。

如果缺少 Apple 编译工具，请打开「终端」运行 `xcode-select --install`。如果缺少 CMake 或 CTest，请从 [CMake 官网](https://cmake.org/download/) 下载适用于本机的 macOS 安装镜像，将 CMake.app 拖入「应用程序」。所需 CMake 版本为 3.22 或更高。安装完毕后再次双击入口即可。

也可以在解压后的 EarLab 目录中通过终端执行：

```bash
bash scripts/build-macos.command
```

脚本按 macOS 12.0 部署目标编译 Apple Silicon 和 Intel 两种架构，在当前 Mac 上运行 CTest 和音量补偿自检，验证双架构、应用签名及磁盘映像。成功后可在 `dist/macos/` 找到 DMG 和 SHA256 清单。

打开 DMG，将 **EarLab Engine.app** 拖入 **Applications**。首次运行后可以使用内置噪声完成一轮 EQ 训练，并从设置页导出报告。插件库支持系统与用户目录下的 VST3。第三方插件应提供与当前应用进程相同的架构。

构建脚本使用本地 ad-hoc 签名。对外发布需要另外配置 Developer ID 签名与 Apple 公证。分发时请保留项目许可和对应源码。本构建包在 Windows 上整理，macOS 双架构编译、音频设备及第三方插件实测需在 Mac 上完成。
