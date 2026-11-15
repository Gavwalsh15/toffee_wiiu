pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: page
    objectName: "connectPage"
    required property var backend
    required property var workspace
    required property var transport
    required property var flasher
    required property var preferences
    property bool hasSource: false
    spacing: 0
    Rectangle {
        Layout.fillWidth: true; Layout.bottomMargin: 14
        implicitHeight: secondScreenRow.implicitHeight + 24
        color: "#292929"; radius: 4
        RowLayout {
            id: secondScreenRow
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 20
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Label { text: "Second screen"; font.weight: Font.DemiBold }
                Label {
                    text: page.preferences.secondScreen
                        ? page.workspace.secondScreen.status
                        : "Use the GamePad as an extra monitor. Toffee connects and starts the stream by itself."
                    color: "#a2a2a2"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
            }
            Switch {
                Accessible.name: "Use the GamePad as a second screen"
                checked: page.preferences.secondScreen
                onToggled: page.preferences.secondScreen = checked
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true; implicitHeight: 38; color: "#2b2b2b"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
            Label { text: "Preview"; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Label {
                text: page.transport.pairingCode ? "Pairing"
                    : page.hasSource ? (page.transport.streaming ? "Sending to ESP" : "Local preview") : "No source"
                color: "#a6a6a6"
                font.pixelSize: 12
            }
        }
    }
    Rectangle {
        id: canvas
        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 160
        color: "#111111"; clip: true
        Rectangle {
            id: frame
            width: Math.min(canvas.width - 32, (canvas.height - 24) * 864 / 480)
            height: width * 480 / 864
            anchors.centerIn: parent; color: "#151515"; border.color: "#282828"
            Column {
                visible: !page.hasSource
                anchors.centerIn: parent; spacing: 12
                Label {
                    text: "\ue714"
                    font.family: "Segoe Fluent Icons"
                    font.pixelSize: 30
                    color: "#606060"
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Label { text: "No video source"; font.pixelSize: 15; color: "#b0b0b0"; anchors.horizontalCenter: parent.horizontalCenter }
                Label {
                    text: "Choose a source below to preview it."
                    color: "#737373"
                    font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
            ScenePreview { objectName: "scenePreview"; anchors.fill: parent; visible: page.hasSource; transport: page.transport }
            Rectangle {
                anchors.fill: parent; anchors.margins: 1
                visible: page.transport.pairingCode !== ""
                color: "#151515"
                Column {
                    anchors.centerIn: parent; spacing: 14
                    Label { text: "Pair a new GamePad"; font.pixelSize: 15; color: "#b0b0b0"; anchors.horizontalCenter: parent.horizontalCenter }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter; spacing: 22
                        Repeater {
                            model: page.transport.pairingCode.split("")
                            Label {
                                required property string modelData
                                text: modelData
                                font.pixelSize: Math.max(32, frame.height / 6)
                                color: palette.highlight
                            }
                        }
                    }
                    Label {
                        text: "Press SYNC on the GamePad, then enter these symbols."
                        color: "#737373"; font.pixelSize: 12
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true; implicitHeight: 34; color: "#252525"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
            Label { text: "Target  864 × 480 / 59.94 fps"; color: "#999999"; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Label { text: page.transport.status; color: "#999999"; font.pixelSize: 11 }
        }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.topMargin: 18; Layout.maximumHeight: 110; spacing: 18
        ColumnLayout {
            Layout.fillWidth: true; Layout.preferredWidth: 1; spacing: 10
            Label { text: "Source"; font.weight: Font.DemiBold }
            SelectBox {
                Layout.fillWidth: true
                model: ["No source", "Interactive demo", "Display capture", "Window capture"]
                enabled: !page.transport.busy
                currentIndex: page.hasSource ? page.transport.sceneMode + 1 : 0
                onActivated: { page.hasSource = currentIndex > 0; page.transport.sceneMode = Math.max(0, currentIndex - 1) }
            }
            RowLayout {
            ActionButton {
                text: page.transport.sceneMode !== StreamController.Interactive ? (page.transport.paused ? "Resume capture" : "Freeze capture") : (page.transport.paused ? "Resume motion" : "Pause motion")
                enabled: page.hasSource
                onClicked: page.transport.paused = !page.transport.paused
            }
            ActionButton {
                text: page.transport.streaming ? "Stop stream" : "Start stream"
                primary: !page.transport.streaming
                enabled: page.transport.streaming || (page.hasSource && page.transport.sourceReady && page.transport.ready)
                onClicked: {
                    if (page.transport.streaming) {
                        page.workspace.secondScreen.hold()
                        page.transport.stopStream()
                    } else {
                        page.workspace.secondScreen.held = false
                        page.transport.startStream()
                    }
                }
            }
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#3b3b3b" }
        ColumnLayout {
            Layout.fillWidth: true; Layout.preferredWidth: 1; spacing: 10
            Label { text: "Device"; font.weight: Font.DemiBold }
            SelectBox {
                Layout.fillWidth: true
                model: page.backend.ports; textRole: "label"
                currentIndex: count ? page.workspace.selectedPortIndex : -1
                displayText: count ? currentText : "No serial ports found"
                enabled: count > 0 && !page.transport.connected && !page.flasher.flashing
                onActivated: { page.workspace.portPicked = true; page.workspace.selectedPortIndex = currentIndex }
            }
            RowLayout {
                ActionButton { text: "Refresh"; enabled: !page.transport.connected && !page.flasher.flashing; onClicked: page.backend.refreshPorts() }
                ActionButton {
                    text: "Restart ESP"
                    enabled: page.transport.connected && !page.transport.busy && !page.flasher.flashing
                    onClicked: page.transport.restartDevice()
                }
                ActionButton {
                    text: "Pair new GamePad"
                    enabled: page.transport.connected && !page.transport.busy && !page.flasher.flashing
                    onClicked: page.transport.forgetPairing()
                }
                ActionButton {
                    text: page.transport.connected ? "Disconnect" : "Connect"
                    primary: !page.transport.connected
                    enabled: !page.flasher.flashing && (page.transport.connected || page.backend.ports.length > 0)
                    onClicked: {
                        if (page.transport.connected) {
                            page.workspace.secondScreen.hold()
                            page.transport.disconnectDevice()
                        } else {
                            page.workspace.secondScreen.held = false
                            page.transport.connectDevice(page.backend.ports[page.workspace.selectedPortIndex].name)
                        }
                    }
                }
            }
        }
    }
    RowLayout {
        visible: page.hasSource && page.transport.sceneMode !== StreamController.Interactive
        Layout.fillWidth: true; Layout.topMargin: 10
        Label { text: page.transport.sceneMode === StreamController.DisplayCapture ? "Display" : "Window (keep it visible)"; color: "#999999" }
        SelectBox {
            Layout.fillWidth: true; model: page.transport.captureTargets; textRole: "label"
            currentIndex: page.transport.captureTarget
            displayText: count ? currentText : "No capture targets available"
            enabled: !page.transport.busy && count > 0
            onActivated: page.transport.captureTarget = currentIndex
        }
        ActionButton { text: "Refresh sources"; enabled: !page.transport.busy; onClicked: page.transport.refreshCaptureTargets() }
    }
    readonly property string statusLine: {
        if (page.transport.captureError)
            return page.transport.captureError
        if (page.transport.error)
            return page.transport.error
        return page.transport.metrics
    }
    readonly property string hintLine: {
        if (page.transport.pairingCode)
            return "Press SYNC on the GamePad and enter the symbols shown above."
        if (page.transport.connected && !page.transport.ready)
            return "Turn on the GamePad. A paired GamePad connects by itself; for a new one, press Pair new GamePad."
        if (page.hasSource && page.transport.sceneMode === StreamController.Interactive)
            return "Left stick / D-pad: move · Right stick: target · A: boost · B: reset · X: colour"
        return "ACK measures ESP completion, not GamePad display latency."
    }
    Label {
        text: page.statusLine
        color: page.transport.error || page.transport.captureError ? "#e8a29c" : "#939393"
        font.pixelSize: 12
        Layout.topMargin: 12
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    Label {
        text: page.hintLine
        color: "#777777"
        font.pixelSize: 11
        Layout.topMargin: 4
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
}


