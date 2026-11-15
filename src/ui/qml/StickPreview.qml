import QtQuick
import QtQuick.Controls

Item {
    id: stick
    property string label: ""
    property real axisX: 0
    property real axisY: 0
    property bool interactive: true
    implicitWidth: 130
    implicitHeight: 170
    Rectangle {
        id: ring
        width: 114
        height: 114
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        color: "#222222"
        radius: 57
        border.color: "#555555"
        Rectangle { width: 1; height: parent.height; anchors.centerIn: parent; color: "#383838" }
        Rectangle { height: 1; width: parent.width; anchors.centerIn: parent; color: "#383838" }
        Rectangle {
            width: 40; height: 40; radius: 20
            x: (ring.width - width) / 2 + stick.axisX * 34
            y: (ring.height - height) / 2 + stick.axisY * 34
            color: pad.pressed ? "#f4c38a" : "#444444"
            border.color: "#777777"
        }
        MouseArea {
            id: pad
            anchors.fill: parent
            preventStealing: true
            enabled: stick.interactive
            function updateAxis(mouse) {
                let dx = (mouse.x - width / 2) / 34
                let dy = (mouse.y - height / 2) / 34
                let length = Math.max(1, Math.sqrt(dx * dx + dy * dy))
                stick.axisX = dx / length
                stick.axisY = dy / length
            }
            onPressed: mouse => updateAxis(mouse)
            onPositionChanged: mouse => { if (pressed) updateAxis(mouse) }
            onReleased: { stick.axisX = 0; stick.axisY = 0 }
            onCanceled: { stick.axisX = 0; stick.axisY = 0 }
        }
    }
    Column {
        anchors.top: ring.bottom
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 5
        Label { text: stick.label; anchors.horizontalCenter: parent.horizontalCenter }
        Label { text: "X " + stick.axisX.toFixed(2) + "   Y " + stick.axisY.toFixed(2); font.pixelSize: 11; color: "#999999"; anchors.horizontalCenter: parent.horizontalCenter }
    }
}

