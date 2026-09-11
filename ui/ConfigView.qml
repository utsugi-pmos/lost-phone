// SPDX-License-Identifier: GPL-2.0-or-later
//
// The whole settings screen, used by both front-ends: the Settings module and
// the standalone app. Everything arrives through `backend`, so this file knows
// nothing about which of the two is showing it. It is a plain ColumnLayout --
// both frames (SimpleKCM and ScrollablePage) provide the scrolling.
//
// The screen is laid out as the capability matrix it is: one section per
// channel, one switch per power. Nothing is granted by turning the feature on;
// every power is granted by hand, and the two that can be turned against you --
// telling a stranger where you are, and locking you out -- start off.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property QtObject backend

    spacing: Kirigami.Units.smallSpacing

    // The daemon is polled rather than signalled: it may be started or stopped
    // from outside this app entirely (the installer does exactly that), and a
    // screen that says "active" about a process that died is worse than one
    // that takes two seconds to notice.
    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: root.backend.refreshDaemon()
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: true
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Information
        text: i18n("An SMS with your key makes the phone ring at full volume even when silenced, or reply with where it is. It works with no data and no internet, which is exactly when you need it. Careful: the key travels in the clear over the carrier's network and works from any number, so anyone who finds it out can make your phone ring.")
    }

    // The feature exists only while the daemon runs. The settings file on its
    // own does nothing, so a UI that looked configured while nothing listened
    // would be a lie -- and this is a feature you find out about on the worst
    // possible day.
    // The first thing you see if the phone is held. It is not just one more
    // notice: it is the state in which the phone ignores your trusted networks
    // and will not go quiet without the PIN, and you have to be able to get out
    // of it from here.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.backend.lostModeActive
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Error
        text: i18n("This phone is in LOST MODE: it always locks, ignores your trusted networks and will not go quiet without the PIN.")
        actions: [
            Kirigami.Action {
                text: i18n("Leave lost mode")
                icon.name: "unlock"
                onTriggered: root.backend.leaveLostMode()
            }
        ]
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: !root.backend.daemonRunning
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Error
        text: i18n("The service is not running, so the phone will not obey any SMS. Start it with: systemctl --user enable --now lost-phoned.service")
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Enable «Find my phone»")
            wrapMode: Text.WordWrap
        }

        QQC2.Switch {
            checked: root.backend.enabled
            onToggled: root.backend.setEnabled(checked)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        enabled: root.backend.enabled
        opacity: enabled ? 1 : 0.5

        // --------------------------------------------------------- en casa
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("Find it at home")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            text: i18n("The case that actually happens: it is on the sofa. No server, no internet and no SMS — the phone announces itself on your wifi and you find it from the computer with «find_it-mi-phone sonar».")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Let it be found on the home network")
                wrapMode: Text.WordWrap
            }

            QQC2.Switch {
                checked: root.backend.lanEnabled
                onToggled: root.backend.setLanEnabled(checked)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing
            enabled: root.backend.lanEnabled

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.backend.lanEnabled && root.backend.lanToken.length > 0
                type: Kirigami.MessageType.Information
                text: i18n("From the computer, just once: «setup/find_it-mi-phone --emparejar». It brings the token over the cable, with nothing to type.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Find it with a link")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("To open it in the browser, put it on a home-automation button or call it from a script. Off by default: the key goes in the address, and that ends up in the history and logs of everything that sees it.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.lanSimpleEnabled
                    onToggled: root.backend.setLanSimpleEnabled(checked)
                }
            }

            // The COMPLETE link, with this phone's address inside it. A template
            // with «<phone>» would leave the user exactly the part they cannot
            // know by heart, and on top of that the part that changes network to
            // network.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.backend.lanSimpleEnabled && root.backend.lanSimpleUrl.length > 0
                type: Kirigami.MessageType.Positive
                text: i18n("Your link:\n%1\n\nSwap «sonar» for «parar» and that is it: this link only rings and silences, never tells where it is or locks. The key goes in the address and ends up in the browser history and the router logs; for anything else you need the token. It only works inside your network.",
                           root.backend.lanSimpleUrl)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                visible: root.backend.lanSimpleEnabled

                QQC2.TextField {
                    Layout.fillWidth: true
                    text: root.backend.lanSimpleKey
                    placeholderText: i18n("link key")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onEditingFinished: root.backend.setLanSimpleKey(text)
                }

                QQC2.Button {
                    text: i18n("Generate")
                    icon.name: "roll"
                    onClicked: root.backend.setLanSimpleKey(root.backend.suggestKey())
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Can be rung from home")
                    wrapMode: Text.WordWrap
                }

                QQC2.Switch {
                    checked: root.backend.lanAllowRing
                    onToggled: root.backend.setLanAllowRing(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Can say where it is from home")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Off by default. Being on your wifi does not prove it is you: guests, neighbours, anything plugged into the router.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.lanAllowLocate
                    onToggled: root.backend.setLanAllowLocate(checked)
                }
            }
        }

        // ------------------------------------------------------------------ SMS
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("Commands over SMS")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Obey commands that arrive by SMS")
                wrapMode: Text.WordWrap
            }

            QQC2.Switch {
                checked: root.backend.smsEnabled
                onToggled: root.backend.setSmsEnabled(checked)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing
            enabled: root.backend.smsEnabled

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Your key. Without it the channel is dead, even if everything else is on.")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    Layout.fillWidth: true
                    text: root.backend.smsKey
                    placeholderText: i18n("no key")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onEditingFinished: root.backend.setSmsKey(text)
                }

                QQC2.Button {
                    text: i18n("Generate")
                    icon.name: "roll"
                    onClicked: root.backend.setSmsKey(root.backend.suggestKey())
                }
            }

            // The one thing the user has to remember on the day it matters,
            // written out with their actual key already in it.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.backend.smsKey.length > 0
                type: Kirigami.MessageType.Positive
                text: i18n("Send to this phone, from any telephone:\n«%1 ring» · «%1 where» · «%1 status»", root.backend.smsKey)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Can ring it")
                    wrapMode: Text.WordWrap
                }

                QQC2.Switch {
                    checked: root.backend.smsAllowRing
                    onToggled: root.backend.setSmsAllowRing(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Can ask where it is")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Replies with the position to whoever asks. On by default: it is THE question you send by SMS, and SMS is all that is left when the phone has run out of data. What protects it is your key.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.smsAllowLocate
                    onToggled: root.backend.setSmsAllowLocate(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Can lock it")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Dangerous: anyone who finds out your key could lock you out of the phone. You only leave lost mode with the PIN or from the panel, never by SMS.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.smsAllowLock
                    onToggled: root.backend.setSmsAllowLock(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Reply by SMS")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Each reply is an SMS your carrier charges you for.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.smsReply
                    onToggled: root.backend.setSmsReply(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Delete the command SMS")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Otherwise the command stays in view in the messages app.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.smsDeleteAfter
                    onToggled: root.backend.setSmsDeleteAfter(checked)
                }
            }
        }

        // ------------------------------------------------------------- relay
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("Your server")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            text: i18n("It is the only thing that works with the phone out on the street: SMS cannot show you a map, and your laptop is not reachable from outside your home either. The phone connects outward to a server of yours, and you open the panel from any browser.")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Talk to my server")
                wrapMode: Text.WordWrap
            }

            QQC2.Switch {
                checked: root.backend.relayEnabled
                onToggled: root.backend.setRelayEnabled(checked)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing
            enabled: root.backend.relayEnabled

            QQC2.TextField {
                Layout.fillWidth: true
                text: root.backend.relayUrl
                placeholderText: i18n("https://find.yourdomain.com")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onEditingFinished: root.backend.setRelayUrl(text)
            }

            // Paired and connected are two different questions, and both are
            // shown: a phone that paired last month and has not reached the
            // server since is exactly the failure you need to see.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.backend.relayPaired
                type: root.backend.relayConnected ? Kirigami.MessageType.Positive
                                                  : Kirigami.MessageType.Warning
                text: root.backend.relayConnected
                    ? i18n("Paired and talking to your server right now.")
                    : i18n("Paired, but right now it is NOT reaching the server. With data or wifi it will come back on its own.")
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.backend.pairingError.length > 0
                type: Kirigami.MessageType.Error
                text: i18n("Pairing failed: %1", root.backend.pairingError)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    id: campoCodigo
                    Layout.fillWidth: true
                    placeholderText: i18n("the 4 numbers from the panel")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                }

                QQC2.Button {
                    text: root.backend.relayPaired ? i18n("Re-pair") : i18n("Pair")
                    icon.name: "network-connect"
                    enabled: campoCodigo.text.length > 0 && root.backend.relayUrl.length > 0
                    onClicked: {
                        root.backend.pair(campoCodigo.text)
                        campoCodigo.text = ""
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Open your panel, press «Generate code» and type the four numbers here. They last ten minutes, work once, and die on the third failed attempt.")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("How often it checks in")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Every minute you set here is a minute the phone does NOT hear you: you press «Ring» on the panel and the command waits until the next heartbeat. «Always connected» delivers it at once and is the recommended option; the rest is battery in exchange for a delay.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }
            }

            QQC2.ComboBox {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                model: [
                    { text: i18n("Always connected — obeys instantly (recommended)"), minutos: 0 },
                    { text: i18n("Every 5 min — may take 5 min to obey"), minutos: 5 },
                    { text: i18n("Every 15 min — may take 15 min"), minutos: 15 },
                    { text: i18n("Every 30 min — may take half an hour"), minutos: 30 },
                    { text: i18n("Every hour — may take an hour"), minutos: 60 }
                ]
                textRole: "text"
                currentIndex: {
                    const m = root.backend.relayIdleMinutes
                    if (m <= 0) return 0
                    if (m <= 5) return 1
                    if (m <= 15) return 2
                    if (m <= 30) return 3
                    return 4
                }
                onActivated: root.backend.setRelayIdleMinutes(model[currentIndex].minutos)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("The panel can ring it")
                    wrapMode: Text.WordWrap
                }

                QQC2.Switch {
                    checked: root.backend.relayAllowRing
                    onToggled: root.backend.setRelayAllowRing(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("The panel can ask where it is")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Off by default. Turn it on and your server will be able to ask it for the position.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.relayAllowLocate
                    onToggled: root.backend.setRelayAllowLocate(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("The panel can lock it")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("It leaves it in lost mode with a message for whoever finds it. It is the only place from which it can also be unlocked.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.relayAllowLock
                    onToggled: root.backend.setRelayAllowLock(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("The panel can power it off")
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Off by default, and only from the panel. It is the one command that turns off all the others: a powered-off phone does not ring, does not say where it is and obeys nobody, not even you. To turn it back on you have to go to it.")
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: root.backend.relayAllowPower
                    onToggled: root.backend.setRelayAllowPower(checked)
                }
            }
        }

        // ------------------------------------------------------------- ntfy
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("If you already have an ntfy")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            text: i18n("For anyone who already self-hosts an ntfy and does not want to stand up another server. The phone listens on one topic and replies on another, so the reply reaches you as a notification on whatever phone you have with you.")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Obey commands that arrive by ntfy")
                wrapMode: Text.WordWrap
            }

            QQC2.Switch {
                checked: root.backend.ntfyEnabled
                onToggled: root.backend.setNtfyEnabled(checked)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing
            enabled: root.backend.ntfyEnabled

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: root.backend.ntfyEnabled
                type: root.backend.ntfyConnected ? Kirigami.MessageType.Positive
                                                 : Kirigami.MessageType.Warning
                text: root.backend.ntfyConnected
                      ? i18n("Listening. Publish «%1 ring» on the commands topic.", root.backend.ntfyKey)
                      : i18n("No connection to the ntfy server.")
            }

            QQC2.TextField {
                Layout.fillWidth: true
                text: root.backend.ntfyServer
                placeholderText: i18n("https://ntfy.sh")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                onEditingFinished: root.backend.setNtfyServer(text)
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Topic it listens on for commands")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
            }

            QQC2.TextField {
                Layout.fillWidth: true
                text: root.backend.ntfyTopic
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onEditingFinished: root.backend.setNtfyTopic(text)
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Topic it replies on. Subscribe to this one from the ntfy app.")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
            }

            QQC2.TextField {
                Layout.fillWidth: true
                text: root.backend.ntfyReplyTopic
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onEditingFinished: root.backend.setNtfyReplyTopic(text)
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("This channel's own key. On a public ntfy the topic name is the only door, and it can be guessed.")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    Layout.fillWidth: true
                    text: root.backend.ntfyKey
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onEditingFinished: root.backend.setNtfyKey(text)
                }

                QQC2.Button {
                    text: i18n("Generate")
                    icon.name: "roll"
                    onClicked: root.backend.setNtfyKey(root.backend.suggestKey())
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Can ring it")
                    wrapMode: Text.WordWrap
                }

                QQC2.Switch {
                    checked: root.backend.ntfyAllowRing
                    onToggled: root.backend.setNtfyAllowRing(checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Can ask where it is")
                    wrapMode: Text.WordWrap
                }

                QQC2.Switch {
                    checked: root.backend.ntfyAllowLocate
                    onToggled: root.backend.setNtfyAllowLocate(checked)
                }
            }
        }

        // --------------------------------------------------------------- sound
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("The sound")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            text: i18n("It rings even if the phone is silenced, and when it stops it leaves the volume as it was. To silence it you have to unlock the phone and press «Stop», or do it from the panel. It cannot be done by SMS: whoever has the phone in hand would read the key on the screen.")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Melody")
                wrapMode: Text.WordWrap
            }

            QQC2.ComboBox {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 11
                model: root.backend.sounds
                textRole: "name"
                valueRole: "path"
                currentIndex: {
                    const lista = root.backend.sounds
                    for (let i = 0; i < lista.length; i++) {
                        if (lista[i].path === root.backend.ringSound) {
                            return i
                        }
                    }
                    return 0
                }
                onActivated: root.backend.setRingSound(currentValue)
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            text: i18n("It loops until you stop it, so what matters is how it sounds chained to itself. A short melody is easier to locate by ear: the ear places sound by its onsets, and a short one gives many more.")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                text: i18n("Volume")
                wrapMode: Text.WordWrap
            }

            QQC2.Slider {
                Layout.fillWidth: true
                from: 10
                to: 100
                stepSize: 5
                snapMode: QQC2.Slider.SnapAlways
                value: root.backend.ringVolume
                onMoved: root.backend.setRingVolume(Math.round(value))
            }

            QQC2.Label {
                text: i18n("%1%", root.backend.ringVolume)
                font: Kirigami.Theme.smallFont
                Layout.minimumWidth: Kirigami.Units.gridUnit * 2
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("How long it rings")
                wrapMode: Text.WordWrap
            }

            QQC2.SpinBox {
                from: 10
                to: 600
                stepSize: 10
                value: root.backend.ringSeconds
                textFromValue: function(value) { return i18n("%1 s", value) }
                valueFromText: function(text) { return parseInt(text) }
                onValueModified: root.backend.setRingSeconds(value)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                Layout.fillWidth: true
                text: i18n("Test for 3 seconds")
                icon.name: "audio-volume-high"
                enabled: root.backend.daemonRunning && !root.backend.ringing
                onClicked: root.backend.testRing()
            }

            QQC2.Button {
                Layout.fillWidth: true
                text: i18n("Stop")
                icon.name: "media-playback-stop"
                enabled: root.backend.ringing
                onClicked: root.backend.stopRing()
            }
        }

        // ----------------------------------------------------------- localizar
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("How it is located")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            text: i18n("They are tried in order and the best one that answers wins. Indoors GPS does not get a fix and the cell does, so removing one is not fine-tuning: it is going blind in a specific situation.")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        Repeater {
            model: [
                { label: i18n("GPS"), hint: i18n("Precise, slow, and does not work indoors"), key: "gnss" },
                { label: i18n("Wifi networks it sees"), hint: i18n("Tells whether it is at home even with no GPS"), key: "wifi" },
                { label: i18n("Cell tower"), hint: i18n("Approximate, but works with no data and no GPS"), key: "cell" },
                { label: i18n("Last known position"), hint: i18n("All that is left when the battery has already died"), key: "last" }
            ]

            delegate: RowLayout {
                id: rung

                required property var modelData

                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: rung.modelData.label
                        wrapMode: Text.WordWrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: rung.modelData.hint
                        wrapMode: Text.WordWrap
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                }

                QQC2.Switch {
                    checked: {
                        switch (rung.modelData.key) {
                        case "gnss": return root.backend.locateGnss
                        case "wifi": return root.backend.locateWifi
                        case "cell": return root.backend.locateCell
                        default: return root.backend.locateLastKnown
                        }
                    }
                    onToggled: {
                        switch (rung.modelData.key) {
                        case "gnss": root.backend.setLocateGnss(checked); break
                        case "wifi": root.backend.setLocateWifi(checked); break
                        case "cell": root.backend.setLocateCell(checked); break
                        default: root.backend.setLocateLastKnown(checked); break
                        }
                    }
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.backend.lastKnownSummary.length > 0
            type: Kirigami.MessageType.Information
            text: i18n("Last saved position: %1", root.backend.lastKnownSummary)
        }
    }
}
