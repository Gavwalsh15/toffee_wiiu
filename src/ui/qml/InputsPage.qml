pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page
    required property var preferences
    required property var input
    required property var transport
    clip: true; contentWidth: availableWidth
    Binding { target: page.input; property: "deadZone"; value: page.preferences.deadZone }
    // Second screen mode always needs touch to work as a mouse.
    Binding { target: page.input; property: "touchAsMouse"; value: page.preferences.touchAsMouse || page.preferences.secondScreen }
    Binding { target: page.input; property: "realTouch"; value: page.preferences.realTouch }
    Binding { target: page.input; property: "xboxLayout"; value: page.preferences.xboxLayout }
    Binding { target: page.input; property: "virtualPad"; value: page.preferences.virtualPad }
    readonly property var controls: page.input.controls
    function mask(name) {
        const found = controls.find(control => control.name === name)
        return found ? found.mask : 0
    }
    ColumnLayout {
        width: page.availableWidth; spacing: 24
        Panel {
            title: "GamePad inputs"
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 230; color: "#191919"; border.color: "#363636"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 20; spacing: 16
                    StickPreview {
                        label: "Left stick"; Layout.fillWidth: true; interactive: false
                        axisX: page.input.axes[0]; axisY: -page.input.axes[1]
                    }
                    ColumnLayout {
                        Layout.preferredWidth: 160; spacing: 12
                        Label {
                            text: page.input.active ? "LIVE INPUT" : "WAITING FOR INPUT"
                            color: page.input.active ? "#91d5ba" : "#888888"
                            font.pixelSize: 10
                            font.letterSpacing: 1
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Grid {
                            columns: 3; spacing: 4; Layout.alignment: Qt.AlignHCenter
                            Repeater {
                                model: [
                                    {name: "", mask: 0}, {name: "↑", mask: page.mask("↑")}, {name: "", mask: 0},
                                    {name: "←", mask: page.mask("←")}, {name: "·", mask: 0}, {name: "→", mask: page.mask("→")},
                                    {name: "", mask: 0}, {name: "↓", mask: page.mask("↓")}, {name: "", mask: 0}
                                ]
                                Rectangle {
                                    id: dpadButton
                                    required property var modelData
                                    width: 32; height: 32; radius: 4
                                    color: modelData.mask && (page.input.buttons & modelData.mask) ? page.palette.highlight : modelData.name ? "#303030" : "transparent"
                                    Label {
                                        anchors.centerIn: parent
                                        text: dpadButton.modelData.name
                                        color: dpadButton.modelData.mask && (page.input.buttons & dpadButton.modelData.mask) ? "#242424" : "#aaaaaa"
                                    }
                                }
                            }
                        }
                    }
                    StickPreview {
                        label: "Right stick"; Layout.fillWidth: true; interactive: false
                        axisX: page.input.axes[2]; axisY: -page.input.axes[3]
                    }
                }
            }
            Flow {
                Layout.fillWidth: true; Layout.topMargin: 12; spacing: 6
                Repeater {
                    model: page.controls
                    delegate: Rectangle {
                        id: controlButton
                        required property var modelData
                        width: modelData.name === "HOME" ? 60 : 38; height: 32; radius: 4
                        color: page.input.buttons & modelData.mask ? page.palette.highlight : "#303030"
                        border.color: "#484848"
                        Label {
                            anchors.centerIn: parent
                            text: controlButton.modelData.name
                            color: page.input.buttons & controlButton.modelData.mask ? "#242424" : "#b5b5b5"
                        }
                    }
                }
            }
            Label {
                text: page.input.active ? page.input.reports + " reports received"
                    : "Connect to the ESP on the Connect page, then turn on the GamePad."
                color: "#939393"; font.pixelSize: 12; Layout.topMargin: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }
        Panel {
            title: "Touchscreen"
            Label {
                text: page.input.touchActive
                    ? "Touch active · " + page.input.touchX + ", " + page.input.touchY
                    : "Touch the GamePad screen to see its position here"
                color: page.input.touchActive ? "#91d5ba" : "#939393"
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            CheckBox {
                text: "Use the GamePad touchscreen on the PC"
                checked: page.preferences.touchAsMouse || page.preferences.secondScreen
                enabled: !page.preferences.secondScreen
                onToggled: page.preferences.touchAsMouse = checked
            }
            CheckBox {
                text: "Send real touch input (off: mouse clicks and drags)"
                enabled: page.preferences.touchAsMouse || page.preferences.secondScreen
                checked: page.preferences.realTouch
                onToggled: page.preferences.realTouch = checked
            }
            Label {
                text: "With the extra monitor on, touching the GamePad touches that monitor. Otherwise it spans all your displays."
                color: "#939393"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 190
                Rectangle {
                    id: touchSurface
                    width: Math.min(parent.width, parent.height * 854 / 480)
                    height: width * 480 / 854
                    anchors.centerIn: parent
                    radius: 6
                    color: "#191919"
                    border.color: "#484848"
                    Rectangle {
                        visible: page.input.touchActive
                        width: 18; height: 18; radius: 9
                        x: Math.max(9, Math.min(touchSurface.width - width - 9,
                                               touchSurface.width * page.input.touchX / 4095 - width / 2))
                        y: Math.max(9, Math.min(touchSurface.height - height - 9,
                                               touchSurface.height * page.input.touchY / 4095 - height / 2))
                        color: page.palette.highlight
                        border.color: "#ffffff"
                    }
                }
            }
        }
        Panel {
            title: "Game controller"
            CheckBox {
                text: "Show the GamePad to Windows as an Xbox 360 controller"
                checked: page.preferences.virtualPad
                onToggled: page.preferences.virtualPad = checked
            }
            Label {
                text: page.input.virtualPadStatus
                color: page.input.virtualPadDriverMissing ? "#e8a07a" : "#939393"
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                visible: page.input.virtualPadDriverMissing
                ActionButton { text: "Install driver"; onClicked: page.input.installVirtualPadDriver() }
                ActionButton { text: "Check again"; onClicked: page.input.retryVirtualPad() }
            }
            FormRow {
                title: "Xbox button positions"
                description: "Off: each GamePad button sends its own letter. On: the bottom GamePad button sends A, as on an Xbox pad."
                Switch {
                    Accessible.name: "Xbox button positions"
                    checked: page.preferences.xboxLayout
                    onToggled: page.preferences.xboxLayout = checked
                }
            }
            Label {
                text: "ZL and ZR act as full triggers. Game rumble is passed on to the GamePad while streaming."
                color: "#939393"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }
        Panel {
            title: "Motion"
            FormRow {
                title: "Accelerometer"
                description: "g · X along the long edge, Y along the short edge, Z through the screen (−1 when flat, face up)"
                Label { text: page.input.accel.map(v => v.toFixed(2)).join(" / "); font.family: "Consolas"; font.pixelSize: 11; color: "#aaaaaa" }
            }
            FormRow {
                title: "Gyroscope"
                description: "degrees/second · X twist about the long edge (upright, push one side away), Y tilt top toward/away, Z steer (spin flat)"
                Label { text: page.input.gyro.map(v => v.toFixed(1)).join(" / "); font.family: "Consolas"; font.pixelSize: 11; color: "#aaaaaa" }
            }
        }
        Panel {
            title: "Rumble"
            FormRow {
                title: "Test rumble"
                description: page.transport.streaming ? "Vibrates the GamePad for half a second" : "Start streaming to test"
                ActionButton {
                    text: "Rumble"
                    enabled: page.transport.streaming
                    onClicked: page.transport.vibrate(500)
                }
            }
        }
        Panel {
            title: "Sticks"
            FormRow {
                title: "Dead zone"
                description: "Applied to live stick readings"
                Slider {
                    Accessible.name: "Stick dead zone"
                    from: 0
                    to: 0.3
                    stepSize: 0.01
                    value: page.preferences.deadZone
                    onMoved: page.preferences.deadZone = value
                    Layout.preferredWidth: 160
                }
                Label { text: Math.round(page.preferences.deadZone * 100) + "%"; Layout.preferredWidth: 32; color: "#b5b5b5" }
            }
            FormRow {
                title: "Raw left / right"
                description: "Approximate range 900–3200; centre 2050"
                Label { text: page.input.rawAxes.join(" / "); font.family: "Consolas"; font.pixelSize: 11; color: "#aaaaaa" }
            }
        }
        ActionButton { text: "Restore dead zone"; onClicked: page.preferences.deadZone = 0.1 }
    }
}
