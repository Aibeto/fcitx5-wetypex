FROM fedora:43

RUN dnf install -y -q \
      gcc gcc-c++ cmake clang git pkgconf-pkg-config boost-devel \
      fcitx5-devel fcitx5-qt-devel libime-devel json-c-devel openssl-devel ca-certificates libcxx-devel \
      qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebengine-devel && \
    dnf clean all
