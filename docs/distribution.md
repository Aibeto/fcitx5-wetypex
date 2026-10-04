# 发行与打包

## 源码边界

正式源码包只包含 WeTypeX 自有代码、构建文件、文档和应用图标。以下内容不得进入源码包或二进制包：

- 原版可执行文件、动态库、输入词典和语言模型。
- 从原版安装包提取的设置与 AI 界面资源。
- 账户身份、配对信息、用户词库、剪贴板和 AI 响应。

`tools/package_release.py` 会生成带固定 gzip 时间戳的源码包与带散列的 Arch `PKGBUILD`，并拒绝不属于源码发行范围的二进制文件。

## 全部发布格式

```bash
packaging/build-packages.sh
```

脚本通过统一的 `/usr` staging 目录生成便携 `tar.zst`，在 Ubuntu 24.04 LTS 与 Fedora 43 容器中分别编译二进制，再使用 nFPM 生成 deb 与 rpm，最后调用 makepkg 生成 Arch 软件包。系统需要预先安装 Docker、`nfpm`、`makepkg`、zstd 和本机构建依赖。

deb 使用 Noble 的 Qt 6.4、Fcitx5 5.1.7 和 glibc 2.39 构建，兼容 Ubuntu 24.04+ 与 Debian 13。Fcitx5 输入插件和 Qt5/Qt6 配置插件采用 `FCITX_INSTALL_ADDONDIR`；私有引擎宿主继续位于 `/usr/lib/fcitx5-wetypex`。配置工具会在 `qt5` 或 `qt6` 子目录加载对应 ABI 的插件。

网络桥接默认静态包含固定版本 libcurl，启用 WS/WSS、OpenSSL 和线程 DNS 解析，避免发行版关闭 WebSocket 功能导致运行时失败。curl 源码版本与 SHA-256 固定在 `cmake/BundledCurl.cmake` 和 Arch 构建文件中，更新时应同步维护。可用 `-DWETYPE_BUNDLED_CURL=OFF` 选择系统 libcurl 8.9+，还需确认其构建启用了 WS/WSS。

也可以只生成源码包与 Arch 构建文件：

```bash
python3 tools/package_release.py
cd dist/arch
makepkg -C -f --noconfirm
```

## AUR

每次正式发布后，发布工作流会使用仓库中的 `packaging/aur/PKGBUILD` 更新
`fcitx5-wetypex` AUR 软件包及其 `.SRCINFO`。工作流支持只重试 AUR 发布，
无需重复构建其他发行格式。

发布前应在干净构建环境中运行依赖检查和完整构建。不要使用 `makepkg -d` 生成正式发布物。

## 版本一致性

发布版本必须同时更新：

- `CMakeLists.txt` 的 `project(... VERSION ...)`。
- `packaging/PKGBUILD.in` 的 `pkgver`。
- `tools/package_release.py` 的 `version`。
- 发布修订时同步更新 `packaging/PKGBUILD.in`、AUR `PKGBUILD` 的
  `pkgrel` 与 `packaging/nfpm.yaml` 的 `release`。
- README 安装示例。

## 安装布局

系统级软件文件安装到 `/usr`，用户运行时与状态始终写入 XDG 用户目录。软件包卸载不得删除用户账户、词库和配置。

## 发布检查

1. 分别在目标 Debian/Ubuntu、Fedora 和 Arch 构建环境完成 Release 构建。
2. 检查软件包中没有原版运行时或账户数据。
3. 检查所有脚本中的安装前缀已由 CMake 替换。
4. 在新用户目录执行 `setup --archive` 和 `setup --check`。
5. 在真实 Fcitx5 会话验证全拼输入、候选选择和设置入口。
6. 检查 deb、rpm、Arch 和便携归档的依赖与文件布局。
7. 将 `dist/SHA256SUMS` 随 GitHub Release 一并发布。

## GitHub Release

推送由 `CMakeLists.txt` 版本和软件包修订号组成的 `vX.Y.Z.B-N` 标签会触发发布工作流。工作流分别在 Arch Linux、Ubuntu 24.04 LTS 和 Fedora 43 环境构建软件包，生成源码包、便携归档与 SHA-256 校验文件，并创建或更新同名 GitHub Release。标签与源码版本或修订号不一致时，工作流会直接失败。
