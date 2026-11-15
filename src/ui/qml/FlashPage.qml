import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ScrollView {
    id: page
    required property var backend
    required property var workspace
    required property var flasher
    required property var transport
    property bool disconnectingForFlash: false
    clip: true
    contentWidth: availableWidth

    function beginFlash() {
        const ports = page.backend.ports
        const index = page.workspace.selectedPortIndex
        if (index < 0 || index >= ports.length)
            return
        const port = ports[index].name
        if (page.transport.connected) {
            page.disconnectingForFlash = true
            page.transport.disconnectDevice()
        } else {
            page.flasher.flash(port)
        }
    }

    Connections {
        target: page.transport
        function onDeviceDisconnected() {
            if (!page.disconnectingForFlash)
                return
            page.disconnectingForFlash = false
            const index = page.workspace.selectedPortIndex
            const ports = page.backend.ports
            if (index >= 0 && index < ports.length)
                page.flasher.flash(ports[index].name)
        }
    }

    Dialog {
        id: confirmFlash
        title: "Flash " + page.flasher.firmwareName + "?"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(420, page.width - 40)
        padding: 0
        // The stock dialog frame draws a square, light border that shows at the corners.
        background: Rectangle {
            radius: 10
            color: "#262626"
            border.width: 1
            border.color: "#3a3a3a"
            antialiasing: true
        }
        Overlay.modal: Rectangle { color: "#a0000000" }
        header: Label {
            text: confirmFlash.title
            font.pixelSize: 15
            font.weight: Font.DemiBold
            topPadding: 18; leftPadding: 20; rightPadding: 20
        }
        contentItem: Label {
            text: "This will replace the firmware on the selected ESP32-C5 bridge with "
                  + page.flasher.firmwareName + " and restart it. Keep the USB cable connected until flashing finishes."
            wrapMode: Text.WordWrap
            color: "#b8b8b8"
            topPadding: 12; bottomPadding: 6; leftPadding: 20; rightPadding: 20
        }
        footer: Item {
            implicitHeight: footerButtons.implicitHeight + 20
            RowLayout {
                id: footerButtons
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 10
                anchors.bottomMargin: 18
                spacing: 10
                ActionButton {
                    text: "Cancel"
                    Layout.fillWidth: true
                    onClicked: confirmFlash.reject()
                }
                ActionButton {
                    text: "Flash firmware"
                    primary: true
                    Layout.fillWidth: true
                    onClicked: confirmFlash.accept()
                }
            }
        }
        onAccepted: page.beginFlash()
    }

    FileDialog {
        id: customBuildDialog
        title: "Select ESP-IDF build manifest"
        fileMode: FileDialog.OpenFile
        nameFilters: ["ESP-IDF flash manifest (flasher_args.json)"]
        onAccepted: page.flasher.loadCustomBuild(selectedFile)
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: 26
        Label {
            text: "Install or restore the bridge firmware over USB."
            color: "#a6a6a6"
            font.pixelSize: 13
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Panel {
            title: "Bridge"
            FormRow {
                title: "Serial port"
                description: "USB Serial/JTAG · ESP32-C5"
                SelectBox {
                    Layout.preferredWidth: Math.min(300, page.availableWidth * 0.5)
                    model: page.backend.ports
                    textRole: "label"
                    currentIndex: count ? page.workspace.selectedPortIndex : -1
                    displayText: count ? currentText : "No serial ports found"
                    enabled: count > 0 && !page.flasher.flashing && !page.disconnectingForFlash
                    onActivated: { page.workspace.portPicked = true; page.workspace.selectedPortIndex = currentIndex }
                }
                ActionButton {
                    text: "Refresh"
                    enabled: !page.flasher.flashing
                    onClicked: page.backend.refreshPorts()
                }
            }
        }
        Panel {
            title: "Firmware package"
            FormRow {
                title: page.flasher.firmwareName
                description: page.flasher.customBuildLoaded
                             ? "Custom ESP-IDF build · " + page.flasher.firmwareSize
                             : "Bundled release · " + page.flasher.firmwareSize
                ActionButton {
                    text: "Choose build…"
                    enabled: !page.flasher.flashing
                    onClicked: customBuildDialog.open()
                }
                ActionButton {
                    visible: page.flasher.customBuildLoaded
                    text: "Use release"
                    enabled: !page.flasher.flashing
                    onClicked: page.flasher.useBundledFirmware()
                }
            }
            Label {
                Layout.fillWidth: true
                text: page.flasher.customBuildLoaded
                      ? "Loaded from flasher_args.json. Keep the ESP-IDF build output intact while flashing."
                      : "The bundled release is ready to use. To flash your own build, choose its ESP-IDF flasher_args.json file."
                color: "#999999"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Label {
                visible: page.flasher.selectionError.length > 0
                Layout.fillWidth: true
                text: page.flasher.selectionError
                color: "#e8a29c"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
        Panel {
            title: "Install"
            subtitle: "The app will put the bridge into download mode, write the firmware, then restart it."
            FormRow {
                Layout.topMargin: 12
                title: "Flash firmware"
                description: page.flasher.status
                ActionButton {
                    text: page.flasher.flashing ? "Flashing…" : "Flash firmware"
                    primary: true
                    enabled: !page.flasher.flashing
                             && !page.transport.busy && !page.disconnectingForFlash
                             && page.backend.ports.length > 0
                    onClicked: confirmFlash.open()
                }
            }
            Rectangle {
                id: progressCard
                readonly property bool done: !page.flasher.flashing && page.flasher.progress >= 100
                readonly property bool failed: !page.flasher.flashing && page.flasher.error.length > 0
                visible: page.flasher.flashing || page.flasher.progress > 0 || failed
                Layout.fillWidth: true
                implicitHeight: progressColumn.implicitHeight + 28
                color: "#292929"
                ColumnLayout {
                    id: progressColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    anchors.topMargin: 14
                    anchors.bottomMargin: 14
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            visible: progressCard.done
                            text: ""
                            font.family: "Segoe Fluent Icons"
                            font.pixelSize: 15
                            color: "#91d5ba"
                        }
                        Label {
                            Layout.fillWidth: true
                            text: progressCard.done ? "Flash complete" : progressCard.failed ? "Flash failed" : page.flasher.status
                            color: progressCard.done ? "#91d5ba" : progressCard.failed ? "#e8a29c" : "#eeeeee"
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: !progressCard.failed
                            text: page.flasher.progress + "%"
                            color: "#a2a2a2"
                            font.pixelSize: 12
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 6
                        radius: 3
                        color: "#3a3a3a"
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(100, page.flasher.progress)) / 100
                            height: parent.height
                            radius: 3
                            color: progressCard.done ? "#91d5ba" : progressCard.failed ? "#e8a29c" : page.workspace.palette.highlight
                            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        }
                    }
                    Label {
                        visible: progressCard.done
                        text: "The bridge is restarting. Open the Connect page to connect when it reappears."
                        color: "#a2a2a2"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: progressCard.failed
                        text: page.flasher.error
                        color: "#e8a29c"
                        font.pixelSize: 12
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 12
                text: "Keep the USB cable connected until flashing finishes. Reconnect on the Connect page after the bridge restarts. If automatic download mode fails, hold BOOT, tap RESET, then retry."
                color: "#858585"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}
