import QtQuick
import QtQuick.Controls

Button {
    id: button
    property bool primary: false
    implicitHeight: 32
    implicitWidth: Math.max(84, contentItem.implicitWidth + 28)
    padding: 8
    contentItem: Text {
        text: button.text
        font: button.font
        color: !button.enabled ? "#787878" : button.primary ? "#25211c" : "#eeeeee"
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 4
        color: !button.enabled ? "#2c2c2c" : button.primary ? (button.down ? Qt.darker(button.palette.highlight, 1.15) : button.palette.highlight)
            : button.down ? "#282828" : button.hovered ? "#404040" : "#353535"
        border.color: button.activeFocus ? button.palette.highlight : button.enabled && !button.primary ? "#505050" : "transparent"
    }
}
