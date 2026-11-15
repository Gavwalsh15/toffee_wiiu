import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: row
    property string title: ""
    property string description: ""
    default property alias controls: fields.data
    Layout.fillWidth: true
    implicitHeight: Math.max(64, labels.implicitHeight + 28, fields.implicitHeight + 24)
    color: "#292929"
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#383838" }
    RowLayout {
        anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 20
        ColumnLayout {
            id: labels
            Layout.fillWidth: true; spacing: 4
            Label { text: row.title; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { visible: row.description.length > 0; text: row.description; color: "#a2a2a2"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        RowLayout { id: fields; spacing: 10 }
    }
}
