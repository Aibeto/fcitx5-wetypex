# 贡献指南

感谢你帮助改进 WeTypeX。提交改动前请先说明要解决的具体行为，并区分 Linux 集成问题、固定原版版本兼容问题和上游服务变化。

## 基本要求

- 不提交原版安装包、二进制、词典、模型、账户数据或第三方生成物。
- 不伪造候选、登录、配对、同步、语音、AI 或传输成功状态。
- 界面变更必须注明参考平台和版本；平台差异要在说明中写清楚。
- ABI 结构变更需要注明适用版本、兼容范围和失败边界。
- 日志、图片和示例数据必须删除 UIN、设备码、传输码、剪贴板及私人文本。

## 代码风格

C/C++ 使用 C++20、两空格缩进和现有命名风格。Python 与 Shell 应保持依赖少、错误即退出，并为创建的账户或用户内容设置严格权限。

## 提交前检查

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
for script in scripts/*.sh.in; do
  case "$script" in *-syncd.sh.in|*-update.sh.in|*-sync.sh.in) continue;; esac
  sh -n "$script"
done
python3 -m py_compile scripts/setup_runtime.py tools/*.py \
  build/fcitx5-wetypex-syncd build/fcitx5-wetypex-update build/fcitx5-wetypex-sync
```

Pull Request 应描述最终行为、适用环境、实际执行过的检查以及仍存在的限制。
