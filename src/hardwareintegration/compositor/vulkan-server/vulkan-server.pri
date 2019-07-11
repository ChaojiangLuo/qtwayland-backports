INCLUDEPATH += $$PWD

QMAKE_USE_PRIVATE += wayland-server

SOURCES += \
    $$PWD/vulkanserverbufferintegration.cpp \
    $$PWD/vulkanwrapper.cpp


HEADERS += \
    $$PWD/vulkanserverbufferintegration.h \
    $$PWD/vulkanwrapper.h

CONFIG += wayland-scanner
WAYLANDSERVERSOURCES += $$PWD/../../../extensions/qt-vulkan-server-buffer-unstable-v1.xml
