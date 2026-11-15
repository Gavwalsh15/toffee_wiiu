import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 32
    implicitWidth: 200
    leftPadding: 10; rightPadding: 30
    contentItem: Text {
        text: control.displayText; font: control.font
        color: control.enabled ? "#e8e8e8" : "#909090"
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    background: Rectangle { radius: 4; color: control.hovered ? "#393939" : "#303030"; border.color: control.activeFocus ? control.palette.highlight : "#505050" }
    indicator: Label { x: control.width - width - 10; anchors.verticalCenter: parent.verticalCenter; text: "\ue70d"; font.family: "Segoe Fluent Icons"; font.pixelSize: 10; color: "#b0b0b0" }
}
