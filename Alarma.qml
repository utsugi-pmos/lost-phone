// SPDX-License-Identifier: GPL-2.0-or-later
//
// The screen that appears when the phone is screaming.
//
// It replaces a notification, and the difference is the point: a notification
// is something you find, this is something that is already in front of you.
// One control, the size of a hand, in the middle of the screen. Nothing to
// read, nothing to aim at.
//
// It cannot appear OVER the lock screen -- nothing can, that is what a lock
// screen is for -- so on a locked phone you unlock and it is there, waiting.
// That is the PIN requirement working as asked, not a limitation to hide.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window

import org.kde.kirigami as Kirigami

QQC2.ApplicationWindow {
    id: root

    visible: true
    visibility: Window.FullScreen
    title: i18n("Find my phone")

    // The window closes itself when the alarm stops for any reason -- the
    // timeout ran out, the panel silenced it, somebody sent 'stop'. A screen
    // demanding you stop something that already stopped is a screen that
    // teaches you to ignore it.
    Timer {
        interval: 700
        running: true
        repeat: true
        onTriggered: {
            lostPhoneBackend.refreshDaemon()
            if (!lostPhoneBackend.ringing) {
                Qt.quit()
            }
        }
    }

    // The alarm's own colour, not the app's. This is the one screen in the
    // package that should not look like a settings page.
    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - Kirigami.Units.gridUnit * 3,
                            Kirigami.Units.gridUnit * 22)
            spacing: Kirigami.Units.gridUnit * 2

            Kirigami.Icon {
                Layout.alignment: Qt.AlignHCenter
                source: "audio-volume-high"
                implicitWidth: Kirigami.Units.iconSizes.huge
                implicitHeight: Kirigami.Units.iconSizes.huge
                opacity: latido.running ? 1 : 0.85

                SequentialAnimation on scale {
                    id: latido
                    running: true
                    loops: Animation.Infinite
                    NumberAnimation { to: 1.12; duration: 420; easing.type: Easing.OutQuad }
                    NumberAnimation { to: 1.0; duration: 420; easing.type: Easing.InQuad }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: i18n("This phone is ringing")
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
                font.weight: Font.DemiBold
            }

            QQC2.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: i18n("You made it ring, or someone who knows your key did.")
                opacity: 0.7
            }

            // The one control. Round, centred and as big as the screen allows:
            // it is pressed by somebody who is holding a phone that will not
            // shut up, which is not the moment for a toolbar.
            QQC2.Button {
                id: parar

                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Kirigami.Units.gridUnit
                implicitWidth: Kirigami.Units.gridUnit * 11
                implicitHeight: Kirigami.Units.gridUnit * 11

                onClicked: {
                    lostPhoneBackend.stopRing()
                    Qt.quit()
                }

                background: Rectangle {
                    radius: width / 2
                    color: parar.down ? Qt.darker(Kirigami.Theme.negativeTextColor, 1.25)
                                      : Kirigami.Theme.negativeTextColor
                    Behavior on color { ColorAnimation { duration: 90 } }
                }

                contentItem: QQC2.Label {
                    text: i18n("STOP")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: Kirigami.Theme.backgroundColor
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 2.1
                    font.weight: Font.Bold
                    font.letterSpacing: 2
                }
            }
        }
    }
}
