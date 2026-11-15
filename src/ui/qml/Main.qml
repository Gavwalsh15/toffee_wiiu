pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ApplicationWindow {
    id: window
    width: 1160; height: 800
    minimumWidth: 900; minimumHeight: 620
    visible: true
    title: "Toffee"
    color: "#202020"
    font.family: "Segoe UI"
    font.pixelSize: 13
    palette.window: "#202020"
    palette.windowText: "#eeeeee"
    palette.text: "#eeeeee"
    palette.base: "#242424"
    palette.button: "#333333"
    palette.buttonText: "#eeeeee"
    palette.highlight: "#f4c38a"
    palette.highlightedText: "#202020"
    palette.dark: "#484848"
    palette.mid: "#484848"
    palette.light: "#555555"
    property int currentPage: 1
    property int selectedPortIndex: 0
    readonly property var pages: ["Flash ESP", "Connect", "Inputs", "Settings"]
    Settings {
        id: userPreferences
        category: "Frontend"
        property real deadZone: 0.1
        property bool touchAsMouse: false
        property bool virtualPad: false
        property bool xboxLayout: false
        property bool realTouch: true
        property bool gamepadMonitor: false
        property bool secondScreen: false
        property bool dsuServer: false
        property int monitorSize: 1
        property bool audioEnabled: true
        property real audioGain: 2.0
    }
    FrontendController { id: frontend }
    DsuServer { id: emulatorInput; input: stream.input; enabled: userPreferences.dsuServer }
    FirmwareFlasher { id: firmwareFlasher }
    VirtualDisplay {
        id: gamepadMonitor
        enabled: userPreferences.gamepadMonitor || userPreferences.secondScreen
        size: userPreferences.monitorSize
        onScreenNameChanged: if (screenName) captureTimer.restart()
    }
    // The GamePad shows its own monitor, so a touch there should land on that monitor.
    Binding { target: stream.input; property: "touchArea"; value: gamepadMonitor.area }
    Timer {
        id: captureTimer
        interval: 1500 // Qt learns about the new screen a moment after Windows does
        onTriggered: stream.captureDisplay(gamepadMonitor.screenName)
    }
    StreamController { id: stream; objectName: "streamController" }
    property alias secondScreen: secondScreen
    SecondScreen {
        id: secondScreen
        preferences: userPreferences; stream: stream; monitor: gamepadMonitor
        frontend: frontend; flasher: firmwareFlasher; workspace: window
    }
    function showCapture() { connectPage.hasSource = true }
    Binding { target: stream; property: "audioEnabled"; value: userPreferences.audioEnabled }
    Binding { target: stream; property: "audioGain"; value: userPreferences.audioGain }
    property bool portPicked: false
    function guessPort() {
        if (!portPicked && frontend.suggestedPort >= 0)
            selectedPortIndex = frontend.suggestedPort
        else if (selectedPortIndex >= frontend.ports.length)
            selectedPortIndex = 0
    }
    Component.onCompleted: guessPort()
    Connections {
        target: frontend
        function onPortsChanged() { window.guessPort() }
    }
    // The bridge re-enumerates after flashing or a replug; keep the list current.
    Timer {
        interval: 1500; repeat: true
        running: !stream.connected && !firmwareFlasher.flashing
        onTriggered: frontend.refreshPorts()
    }
    footer: Rectangle {
        height: 30; color: "#191919"
        Rectangle { width: parent.width; height: 1; color: "#353535" }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18; spacing: 9
            Rectangle { Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: "#898989" }
            Label { text: stream.status; color: stream.ready ? "#91d5ba" : "#b5b5b5"; font.pixelSize: 11 }
            Label { text: "·   ESP32-C5"; color: "#808080"; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Label { text: stream.connected ? stream.portName : "Toffee"; color: "#969696"; font.pixelSize: 11 }
        }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true; color: "#191919"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 4
                RowLayout {
                    Layout.leftMargin: 12; Layout.topMargin: 15; Layout.bottomMargin: 30; spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 25; Layout.preferredHeight: 25; radius: 5; color: window.palette.highlight
                        Label { anchors.centerIn: parent; text: "T"; color: "#28221c"; font.pixelSize: 19; font.weight: Font.DemiBold }
                    }
                    Label { text: "Toffee"; font.pixelSize: 20; font.weight: Font.DemiBold }
                }
                Repeater {
                    model: window.pages
                    delegate: Button {
                        id: navigation
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true; Layout.preferredHeight: 40
                        Layout.topMargin: index === 3 ? 22 : 0
                        onClicked: window.currentPage = index
                        Accessible.name: modelData
                        contentItem: RowLayout {
                            spacing: 12
                            Label {
                                Layout.leftMargin: 13
                                text: ["\ue896", "\ue839", "\ue7fc", "\ue713"][navigation.index]
                                font.family: "Segoe Fluent Icons"
                                font.pixelSize: 17
                                color: "#c3c3c3"
                            }
                            Label {
                                text: navigation.modelData
                                Layout.fillWidth: true
                                font.weight: window.currentPage === navigation.index ? Font.DemiBold : Font.Normal
                            }
                        }
                        background: Rectangle {
                            radius: 4
                            color: window.currentPage === navigation.index ? "#303030" : navigation.hovered ? "#252525" : "transparent"
                            border.color: navigation.activeFocus ? window.palette.highlight : "transparent"
                            Rectangle {
                                visible: window.currentPage === navigation.index
                                width: 3
                                height: 16
                                radius: 1.5
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                color: window.palette.highlight
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Label { text: "Wii U GamePad utility"; color: "#777777"; font.pixelSize: 11; Layout.leftMargin: 12; Layout.bottomMargin: 12 }
            }
        }
        Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: "#353535" }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.margins: 26; spacing: 22
            Label { text: window.pages[window.currentPage]; font.pixelSize: 26; font.weight: Font.DemiBold; Layout.topMargin: 2 }
            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: window.currentPage
                FlashPage { backend: frontend; workspace: window; flasher: firmwareFlasher; transport: stream }
                ConnectPage { id: connectPage; backend: frontend; workspace: window; transport: stream; flasher: firmwareFlasher; preferences: userPreferences }
                InputsPage { preferences: userPreferences; input: stream.input; transport: stream }
                SettingsPage { preferences: userPreferences; monitor: gamepadMonitor; emulatorInput: emulatorInput }
            }
        }
    }
}
