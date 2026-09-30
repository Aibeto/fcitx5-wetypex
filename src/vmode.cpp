#include "common/qt_helpers.hpp"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

static QString stateDir() {
  const auto override = qEnvironmentVariable("WETYPE_STATE_DIR");
  return override.isEmpty() ? QStandardPaths::writableLocation(
                                  QStandardPaths::GenericDataLocation) +
                                  "/fcitx5-wetypex/state"
                            : override;
}
static QString candidateIcon(const QString &name) {
  return QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                "fcitx5-wetypex/ui/candidate-icons/" + name +
                                    ".svg");
}
static void clearLayout(QLayout *layout) {
  while (auto *item = layout->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->hide();
      widget->deleteLater();
    } else if (auto *child = item->layout()) {
      clearLayout(child);
    }
    delete item;
  }
}
static void sendAction(quint64 session, const QString &action,
                       const QString &text = {}) {
  QDir().mkpath(stateDir());
  QSaveFile file(stateDir() + "/vmode-action.json");
  if (!file.open(QIODevice::WriteOnly))
    return;
  QJsonObject value{{"version", QDateTime::currentMSecsSinceEpoch()},
                    {"session", static_cast<qint64>(session)},
                    {"action", action},
                    {"text", text}};
  file.write(QJsonDocument(value).toJson(QJsonDocument::Compact));
  file.commit();
}
static QJsonArray hotwords() {
  QLocalSocket socket;
  socket.connectToServer(stateDir() + "/control.sock");
  if (!socket.waitForConnected(1000))
    return {};
  QJsonObject request{{"session", 1},
                      {"seq", QDateTime::currentMSecsSinceEpoch()},
                      {"epoch", 1},
                      {"op", "hotword_list"}};
  return wetype::localRequest(socket, request, 2000)
      .value("hotwords")
      .toArray();
}
static QPushButton *menuButton(const QString &title, const QString &icon) {
  auto *button = new QPushButton(title);
  button->setIcon(QIcon(candidateIcon(icon)));
  button->setIconSize({22, 22});
  button->setFixedSize(96, 58);
  return button;
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("fcitx5-wetypex-vmode");
  quint64 session =
      argc > 1 ? QString::fromLocal8Bit(argv[1]).toULongLong() : 0;
  int x = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 0;
  int y = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 0;
  QWidget window(nullptr,
                 Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  window.setStyleSheet(
      "QWidget{background:#f7f7f7;color:#202124;font:14px 'Noto Sans CJK SC';}"
      "QPushButton{background:white;border:1px solid "
      "#e7e7e7;border-radius:8px;padding:6px;}"
      "QPushButton:hover{border-color:#23c891;background:#f2fffa;}"
      "QTabWidget::pane{border:0;} QTabBar::tab:selected{color:#23c891;}");
  auto *root = new QVBoxLayout(&window);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(6);
  auto showList = [&](const QString &title,
                      const QList<QPair<QString, QString>> &items) {
    clearLayout(root);
    auto *heading = new QLabel(title);
    root->addWidget(heading);
    auto *list = new QWidget;
    auto *layout = new QVBoxLayout(list);
    layout->setContentsMargins(0, 0, 0, 0);
    for (const auto &[label, text] : items) {
      auto *button = new QPushButton(label);
      QObject::connect(button, &QPushButton::clicked, &window, [&, text] {
        sendAction(session, "commit", text);
        window.close();
      });
      layout->addWidget(button);
    }
    layout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(list);
    root->addWidget(scroll);
    window.resize(430, 330);
  };
  auto *menu = new QHBoxLayout;
  auto *calculator = menuButton("计算", "icon_keybar_calculator");
  auto *clipboard = menuButton("剪贴板", "icon_keybar_clipboard");
  auto *phrases = menuButton("常用语", "icon_keybar_changyongyu");
  auto *symbols = menuButton("符号", "icon_keybar_symbol");
  for (auto *button : {calculator, clipboard, phrases, symbols})
    menu->addWidget(button);
  root->addLayout(menu);
  QObject::connect(calculator, &QPushButton::clicked, &window, [&] {
    sendAction(session, "calculator");
    window.close();
  });
  QObject::connect(clipboard, &QPushButton::clicked, &window, [&] {
    QList<QPair<QString, QString>> items;
    QFile file(stateDir() + "/clipboard-history.json");
    if (file.open(QIODevice::ReadOnly))
      for (const auto &v : QJsonDocument::fromJson(file.readAll()).array())
        items.append({v.toString().left(60), v.toString()});
    showList("剪贴板", items);
  });
  QObject::connect(phrases, &QPushButton::clicked, &window, [&] {
    QList<QPair<QString, QString>> items;
    for (const auto &v : hotwords()) {
      auto o = v.toObject();
      items.append({o.value("words").toString(), o.value("words").toString()});
    }
    showList("常用语", items);
  });
  QObject::connect(symbols, &QPushButton::clicked, &window, [&] {
    clearLayout(root);
    QFile file(QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                      "fcitx5-wetypex/wetypex-symbols.json"));
    if (!file.open(QIODevice::ReadOnly))
      return;
    auto *tabs = new QTabWidget;
    for (const auto &groupValue :
         QJsonDocument::fromJson(file.readAll()).array()) {
      auto group = groupValue.toObject();
      auto *body = new QWidget;
      auto *grid = new QGridLayout(body);
      int n = 0;
      for (const auto &subValue : group.value("groupData").toArray())
        for (const auto &rowValue :
             subValue.toObject().value("subGroupData").toArray())
          for (const auto &symbolValue : rowValue.toArray()) {
            QString text = symbolValue.toString();
            auto *button = new QPushButton(text);
            button->setFixedSize(38, 32);
            QObject::connect(button, &QPushButton::clicked, &window, [&, text] {
              sendAction(session, "commit", text);
              window.close();
            });
            grid->addWidget(button, n / 9, n % 9);
            ++n;
          }
      auto *scroll = new QScrollArea;
      scroll->setWidgetResizable(true);
      scroll->setWidget(body);
      const auto iconName =
          QFileInfo(group.value("iconPath").toString()).baseName();
      tabs->addTab(scroll, QIcon(candidateIcon(iconName)),
                   group.value("groupName").toString());
    }
    root->addWidget(tabs);
    window.resize(450, 360);
  });
  window.adjustSize();
  window.move(x, y);
  window.show();
  return app.exec();
}
