# Find the phone: make it ring and know where it is.
#
# Four artefacts out of two shared static libraries, the same shape as
# smart-unlock:
#
#   lost_phone_common    config schema + the locate cascade + the ringer
#   lost_phone_backend   the editable settings model, over the common lib
#   kcm_lost_phone.so    the Settings module, inside Preferencias
#   lost-phone           a normal application, in the app drawer
#   lost-phoned          the daemon: listens for commands and acts on them
#
# The split is not decoration. A KCM is a plugin tied to the KF6 ABI and an
# update can leave it unable to load; the app needs only Qt and Kirigami, so the
# settings for a feature you reach for in an emergency stay reachable. And the
# daemon is QtCore/DBus only -- it runs for days and never draws, so linking
# QtQuick into it would make the cheap thing expensive.
#
# Built natively on the surya, like smart-unlock:
#   cmake -S . -B build -G Ninja && cmake --build build
cmake_minimum_required(VERSION 3.20)
project(lost-phone VERSION 1.4)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(KF_MIN_VERSION 6.0)
set(QT_MIN_VERSION 6.6)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Widgets DBus)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
	CoreAddons
	Config
	I18n
	KCMUtils
)

qt_standard_project_setup(REQUIRES 6.6)

# --- the shared, UI-less core -----------------------------------------------
# The on-disk schema, the locate cascade and the alarm. The daemon links this,
# which is why it must not pull in Gui/Qml.
add_library(lost_phone_common STATIC
	src/config.cpp
	src/config.h
	src/dbus.h
	src/locator.cpp
	src/locator.h
	src/lanserver.cpp
	src/lanserver.h
	src/locker.cpp
	src/locker.h
	src/lostmode.cpp
	src/lostmode.h
	src/nmea.cpp
	src/process.h
	src/ntfyclient.cpp
	src/ntfyprotocol.cpp
	src/ntfyprotocol.h
	src/ntfyclient.h
	src/nmea.h
	src/relayclient.cpp
	src/relayclient.h
	src/ringer.cpp
	src/ringer.h
)
target_include_directories(lost_phone_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lost_phone_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(lost_phone_common PUBLIC
	Qt::Core
	Qt::DBus
	Qt::Network
	KF6::ConfigCore
)

# --- the editable settings model, shared by both front-ends ------------------
add_library(lost_phone_backend STATIC backend.cpp backend.h)
target_include_directories(lost_phone_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lost_phone_backend PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(lost_phone_backend PUBLIC
	lost_phone_common
	Qt::Core
	Qt::DBus
	KF6::CoreAddons
)

# --- the Settings module -----------------------------------------------------
# kcmutils_add_qml_kcm() GLOB_RECURSEs ui/, so ui/main.qml and ui/ConfigView.qml
# both become the KCM's QML. App.qml is NOT in ui/ on purpose: it belongs to the
# app, and would otherwise be swept into the module too.
kcmutils_add_qml_kcm(kcm_lost_phone SOURCES kcm.cpp)
target_link_libraries(kcm_lost_phone PRIVATE
	lost_phone_backend
	KF6::I18n
	KF6::KCMUtils
	KF6::KCMUtilsQuick
)

# --- the standalone application ----------------------------------------------
qt_add_executable(lost-phone main.cpp)

# ConfigView.qml is shared, but one .qml cannot belong to two QML modules:
# qt_add_qml_module stores the URI in source-file properties, so the KCM's copy
# and the app's would fight. Same fix as smart-unlock -- a build-time copy for
# the app, with ui/ConfigView.qml staying the one file to edit.
configure_file(ui/ConfigView.qml ${CMAKE_CURRENT_BINARY_DIR}/appqml/ConfigView.qml COPYONLY)
configure_file(App.qml ${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml COPYONLY)
configure_file(Alarma.qml ${CMAKE_CURRENT_BINARY_DIR}/appqml/Alarma.qml COPYONLY)

set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml
	PROPERTIES QT_RESOURCE_ALIAS App.qml)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/appqml/ConfigView.qml
	PROPERTIES QT_RESOURCE_ALIAS ConfigView.qml)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/appqml/Alarma.qml
	PROPERTIES QT_RESOURCE_ALIAS Alarma.qml)

qt_add_qml_module(lost-phone
	URI LostPhone
	VERSION 1.0
	QML_FILES
		${CMAKE_CURRENT_BINARY_DIR}/appqml/App.qml
		${CMAKE_CURRENT_BINARY_DIR}/appqml/ConfigView.qml
		${CMAKE_CURRENT_BINARY_DIR}/appqml/Alarma.qml
)

target_link_libraries(lost-phone PRIVATE
	lost_phone_backend
	Qt::Gui
	Qt::Qml
	Qt::Quick
	Qt::QuickControls2
	Qt::Widgets
	KF6::CoreAddons
	KF6::I18n
)

# --- the daemon --------------------------------------------------------------
qt_add_executable(lost-phoned daemon/main.cpp)
target_link_libraries(lost-phoned PRIVATE
	lost_phone_common
	Qt::Core
	Qt::DBus
	KF6::ConfigCore
)

# --- install -----------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS lost-phone lost-phoned
	BUNDLE DESTINATION .
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES org.kde.lostphone.desktop
	DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES org.kde.lostphone.svg
	DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# Installed disabled. `systemctl --user enable` is the setup script's job, so
# installing the package never starts obeying SMS behind the user's back.
#
# The path is literal 'lib/', NOT ${CMAKE_INSTALL_LIBDIR}: under a plain QEMU
# build KDEInstallDirs resolves that to lib64 and abuild then refuses the
# package outright. systemd only ever looks in /usr/lib/systemd/user anyway.
install(FILES lost-phoned.service
	DESTINATION lib/systemd/user)

install(FILES README.md
	DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/lost-phone)
