#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  const QStringList arguments = app.arguments();
  if (arguments.contains("--read-text")) {
    int result = 1;
    QTimer::singleShot(100, &app, [&] {
      const QByteArray text = QGuiApplication::clipboard()->text().toUtf8();
      QFile output;
      if (!text.isEmpty() && text.size() <= 65536 &&
          output.open(stdout, QIODevice::WriteOnly) &&
          output.write(text) == text.size())
        result = 0;
      else if (text.size() > 65536)
        result = 2;
      app.exit();
    });
    app.exec();
    return result;
  }
  if (arguments.contains("--read-image")) {
    int result = 1;
    QTimer::singleShot(100, &app, [&] {
      const QImage image = QGuiApplication::clipboard()->image();
      if (!image.isNull()) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QFile output;
        if (image.save(&buffer, "PNG") && png.size() <= 8388608 &&
            output.open(stdout, QIODevice::WriteOnly) &&
            output.write(png) == png.size())
          result = 0;
        else
          result = 2;
      }
      app.exit();
    });
    app.exec();
    return result;
  }
  QFile input;
  if (!input.open(stdin, QIODevice::ReadOnly))
    return 2;
  QByteArray bytes =
      input.read(arguments.contains("--image")             ? 8388609
                 : arguments.contains("--normalize-image") ? 8388609
                                                           : 65537);
  if (bytes.isEmpty() ||
      bytes.size() > (arguments.contains("--image") ||
                              arguments.contains("--normalize-image")
                          ? 8388608
                          : 65536))
    return 2;
  if (arguments.contains("--normalize-image")) {
    const QImage image = QImage::fromData(bytes);
    QBuffer output;
    output.open(QIODevice::WriteOnly);
    if (image.isNull() || !image.save(&output, "PNG") ||
        output.data().size() > 8388608)
      return 2;
    QFile stream;
    if (!stream.open(stdout, QIODevice::WriteOnly) ||
        stream.write(output.data()) != output.data().size())
      return 2;
    return 0;
  }
  if (arguments.contains("--image")) {
    const QImage image = QImage::fromData(bytes);
    if (image.isNull())
      return 2;
    QGuiApplication::clipboard()->setImage(image, QClipboard::Clipboard);
  } else {
    auto text = QString::fromUtf8(bytes);
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
  }
  // X11 selections are owned by a client. Keep this tiny process alive long
  // enough for a clipboard manager or the target application to request it.
  if (arguments.contains("--notify-ready")) {
    std::puts("ready");
    std::fflush(stdout);
  }
  QTimer::singleShot(60000, &app, &QCoreApplication::quit);
  return app.exec();
}
