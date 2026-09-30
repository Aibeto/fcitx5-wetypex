#pragma once
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <functional>
#include <memory>
#include <utility>

namespace wetype {
inline bool settingBool(const QJsonValue &value, bool fallback = false) {
  // Fcitx configuration pages in older releases saved switches as 0/1.
  return value.isDouble() ? value.toDouble() != 0 : value.toBool(fallback);
}

inline void onProcessDone(QProcess *process, QObject *owner,
                          std::function<void(int, QProcess::ExitStatus)> done) {
  auto completed = std::make_shared<bool>(false);
  auto finish = [completed, done = std::move(done)](
                    int code, QProcess::ExitStatus status) {
    if (!std::exchange(*completed, true))
      done(code, status);
  };
  QObject::connect(process,
                   qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                   owner, finish);
  QObject::connect(process, &QProcess::errorOccurred, owner,
                   [finish](QProcess::ProcessError error) {
                     if (error == QProcess::FailedToStart)
                       finish(-1, QProcess::CrashExit);
                   });
}

inline QJsonObject localRequest(QLocalSocket &socket,
                                const QJsonObject &request, int timeout) {
  const auto bytes =
      QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
  if (socket.write(bytes) != bytes.size())
    return {{"error", "无法发送输入核心请求"}};
  QElapsedTimer deadline;
  deadline.start();
  QByteArray response;
  for (;;) {
    response += socket.readAll();
    if (response.size() > 4194304)
      return {{"error", "输入核心响应过大"}};
    const auto newline = response.indexOf('\n');
    if (newline >= 0) {
      QJsonParseError error;
      const auto document =
          QJsonDocument::fromJson(response.left(newline), &error);
      if (error.error != QJsonParseError::NoError || !document.isObject())
        return {{"error", "输入核心返回了无效响应"}};
      return document.object();
    }
    const int remaining = timeout - int(deadline.elapsed());
    if (remaining <= 0 || !socket.waitForReadyRead(remaining))
      return {{"error", "输入核心没有响应"}};
  }
}
} // namespace wetype
