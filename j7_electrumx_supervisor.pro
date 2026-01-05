QT = core

CONFIG += c++17 cmdline

# Ensure minimum Qt version
greaterThan(QT_MAJOR_VERSION, 6) {
    # OK
} else:equals(QT_MAJOR_VERSION, 6) {
    lessThan(QT_MINOR_VERSION, 8) {
        error("This project requires Qt 6.8 or higher.")
    }
} else {
    error("This project requires Qt 6.8 or higher.")
}

CONFIG += c++17 cmdline

TARGET = electrumx_supervisor
DEFINES += SUPERVISOR_APP=\\\"$${TARGET}\\\"

VERSION = 1.0.0
DEFINES += SUPERVISOR_VERSION=\\\"$${VERSION}\\\"

HEADERS += \
    src/electrumxsupervisor.h

SOURCES += \
    src/electrumxsupervisor.cpp \
    src/main.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

