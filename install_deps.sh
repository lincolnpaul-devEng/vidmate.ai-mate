#!/usr/bin/env bash
set -e

PACKAGES=(
  gcc-16
  g++-16
  libstdc++-16-dev
  build-essential
  cmake
  ninja-build
  extra-cmake-modules
  clang-format
  qt6-base-dev
  qt6-svg-dev
  qt6-multimedia-dev
  qt6-networkauth-dev
  qt6-declarative-dev
  qt6-declarative-private-dev
  qt6-image-formats-plugins
  breeze-icon-theme
  libkf6archive-dev
  libkf6bookmarks-dev
  libkf6codecs-dev
  libkf6config-dev
  libkf6configwidgets-dev
  libkf6coreaddons-dev
  libkf6crash-dev
  libkf6dbusaddons-dev
  libkf6doctools-dev
  libkf6filemetadata-dev
  libkf6guiaddons-dev
  libkf6iconthemes-dev
  libkf6kio-dev
  libkf6newstuff-dev
  libkf6notifications-dev
  libkf6notifyconfig-dev
  libkf6purpose-dev
  libkf6solid-dev
  libkf6textwidgets-dev
  libkf6widgetsaddons-dev
  libkf6xmlgui-dev
  qml6-module-org-kde-desktop
  qml6-module-org-kde-kquickcontrolsaddons
  qml6-module-org-kde-iconthemes
  qml6-module-org-kde-kirigami
  qml6-module-org-kde-coreaddons
  qml6-module-qtquick-controls
  qml6-module-qtquick-layouts
  qml6-module-qtquick-templates
  qml6-module-qtquick-shapes
  qml6-module-qt5compat-graphicaleffects
  libmlt++-dev
  libmlt-dev
  melt
  ffmpeg
  libavformat-dev
  libavcodec-dev
  libavutil-dev
  libswresample-dev
  libswscale-dev
  libavdevice-dev
  libavfilter-dev
  frei0r-plugins
  frei0r-plugins-dev
  ladspa-sdk
  libimath-dev
  libopentimelineio-dev
  libkddockwidgets-qt6-dev
)

echo "Installing Kdenlive build dependencies..."
sudo apt-get install -y "${PACKAGES[@]}"
echo "All dependencies installed successfully!"
