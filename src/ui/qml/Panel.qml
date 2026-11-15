import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    property string title: ""
    property string subtitle: ""
    default property alias content: body.data
    Layout.fillWidth: true
    spacing: 12
    Label { visible: panel.title.length > 0; text: panel.title; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
    Label { visible: panel.subtitle.length > 0; text: panel.subtitle; color: "#a6a6a6"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    ColumnLayout { id: body; Layout.fillWidth: true; spacing: 0 }
}
