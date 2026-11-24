EarLab 0.6.1 — Read Me / 安装说明
=================================

安装 / Install
--------------
两种方式都不需要管理员权限，也不触发 UAC。

A. 便携 ZIP 安装器（推荐，A7 实测路径）
   1) 解压 EarLab-0.6.1-win64-portable.zip 到任意目录；
   2) 双击 Install-EarLab.cmd；
      或运行：powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
   3) 安装到 %LOCALAPPDATA%\Programs\EarLab，并创建开始菜单和
      「已安装的应用」条目。
   静默安装：powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Quiet
   不创建桌面快捷方式（如需：加 -DesktopShortcut）。

B. setup.exe（Inno Setup 备用安装包）
   - 双击 EarLab-0.6.1-win64-setup.exe，按向导安装到同一位置。
   - 静默安装：
       EarLab-0.6.1-win64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
   - 若安全软件（如火绒）拦截未知发布者的安装器，请改用方式 A。

运行要求 / Requirements
-----------------------
- Windows 10 1809+ x64 或 Windows 11 x64。
- 安装包已随附 VC++ 2022 x64 运行库（msvcp140.dll、vcruntime140.dll、
  msvcp140_atomic_wait.dll, vcruntime140_1.dll，app-local），无需另行安装 VC++ Redistributable。
- 可选：VST3 插件用于「插件训练」页。EarLab 不附带任何第三方插件。
- 建议使用 ASIO/WASAPI 音频设备；无可用音频设备时部分训练不可用。

数据 / Data
-----------
- 默认档案：%APPDATA%\EarLab\profile.json
- 同目录还有：assessments.json、plugins.json、plugin-memory.json、
  earlab-plugin.log。
- 卸载默认保留该目录；升级/卸载前建议自行备份。
- EarLab 只在本机读写这些文件，不会上传。

便携模式 / Portable
-------------------
- 解压 ZIP 后运行 EarLab-portable.cmd；档案保存在解压目录内。
- 或直接运行：
    "EarLab Engine.exe" --profile=".\profile.json"
- 删除解压目录即完成卸载；不会写入安装信息。

卸载 / Uninstall
----------------
- 设置 > 应用 > 已安装的应用 > EarLab > 卸载。
- 或运行安装目录中的 unins000.exe（setup.exe 安装）或
  uninstall.ps1 -Quiet（便携 ZIP 安装器）。
- 静默卸载：
    unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
    powershell -NoProfile -ExecutionPolicy Bypass -File uninstall.ps1 -Quiet
- 卸载删除程序文件、开始菜单/桌面快捷方式和「已安装的应用」条目，
  但不会删除 %APPDATA%\EarLab。

签名 / Signing
--------------
- 本构建使用内部开发证书 CN=EarLab Dev 签名；目标机可能显示
  「未知发布者」，SmartScreen 可能拦截。
- 请先用 SHA256SUMS.txt 核对安装包哈希。
- 不要在最终用户机器上安装开发根证书。

许可 / License
--------------
- LICENSE.txt                 EarLab 许可与源码说明（AGPLv3）
- THIRD-PARTY-NOTICES.txt     第三方组件声明（JUCE 8 等）
- SOURCE-OFFER.txt            对应源码获取说明
- 对外商业分发前必须取得 JUCE 商业授权，并使用 CA 证书签名。
