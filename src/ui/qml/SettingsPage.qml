pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page
    required property var preferences
    required property var monitor
    required property var emulatorInput
    clip: true; contentWidth: availableWidth
    ColumnLayout {
        width: page.availableWidth; spacing: 26
        Panel {
            title: "GamePad output"
            FormRow { title: "Video target"; Label { text: "864 × 480"; color: "#aaaaaa" } }
            FormRow { title: "Frame rate"; Label { text: "59.94 fps"; color: "#aaaaaa" } }
            FormRow {
                title: "Audio"
                description: "Plays PC sound on the GamePad. Turn off if video stutters."
                Switch {
                    Accessible.name: "Play PC audio on the GamePad"
                    checked: page.preferences.audioEnabled
                    onToggled: page.preferences.audioEnabled = checked
                }
            }
            FormRow {
                title: "Audio volume"
                description: "Boost if the GamePad is too quiet. Loud peaks are softened, not clipped."
                Slider {
                    Accessible.name: "Audio volume boost"
                    from: 0.5
                    to: 4
                    stepSize: 0.1
                    value: page.preferences.audioGain
                    onMoved: page.preferences.audioGain = value
                    Layout.preferredWidth: 160
                }
                Label { text: Math.round(page.preferences.audioGain * 100) + "%"; Layout.preferredWidth: 40; color: "#b5b5b5" }
            }
        }
        Panel {
            title: "GamePad as a monitor"
            FormRow {
                title: "Extra monitor"
                description: "Adds a monitor to Windows and shows it on the GamePad. Drag windows onto it."
                Switch {
                    Accessible.name: "Use the GamePad as an extra monitor"
                    checked: page.preferences.gamepadMonitor || page.preferences.secondScreen
                    enabled: !page.preferences.secondScreen
                    onToggled: page.preferences.gamepadMonitor = checked
                }
            }
            FormRow {
                title: "Monitor size"
                description: "854 × 480 is a 1:1 picture, the sharpest on the GamePad, but a very small desktop. Larger sizes are shrunk to fit."
                SelectBox {
                    model: ["854 × 480 (sharpest)", "1280 × 720", "1920 × 1080"]
                    currentIndex: page.preferences.monitorSize
                    onActivated: page.preferences.monitorSize = currentIndex
                }
            }
            Label {
                text: page.monitor.status
                color: page.monitor.problem ? "#e8a07a" : "#939393"
                font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            ActionButton {
                visible: !page.monitor.installed || page.monitor.needsUpdate
                enabled: !page.monitor.installing
                text: page.monitor.installed ? "Update the monitor settings" : "Install the driver"
                onClicked: page.monitor.installDriver()
            }
            Label {
                text: "The driver is the open-source Virtual Display Driver. Windows asks for permission once to install it."
                color: "#939393"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }
        Panel {
            title: "Input sharing"
            FormRow {
                title: "Share input over DSU"
                description: "Lets DSU clients on this PC, read the GamePad's buttons, sticks, touch screen and gyro. In the client, add a DSU server at 127.0.0.1, port 26760."
                Switch {
                    Accessible.name: "Share GamePad input over DSU"
                    checked: page.preferences.dsuServer
                    onToggled: page.preferences.dsuServer = checked
                }
            }
            Label {
                visible: page.preferences.dsuServer
                text: page.emulatorInput.status
                color: page.emulatorInput.status.startsWith("Could not") ? "#e8a07a" : "#939393"
                font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }
        Panel {
            title: "Game controller"
            FormRow {
                title: "Xbox 360 controller"
                description: "Shows the GamePad to Windows as an Xbox controller for games. Needs the ViGEmBus driver; the Inputs page can install it."
                Switch {
                    Accessible.name: "Show the GamePad as an Xbox 360 controller"
                    checked: page.preferences.virtualPad
                    onToggled: page.preferences.virtualPad = checked
                }
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
        }
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Label { text: "Toffee " + Qt.application.version; font.weight: Font.DemiBold }
                Label {
                    text: "PC to Wii U GamePad"
                    color: "#939393"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
