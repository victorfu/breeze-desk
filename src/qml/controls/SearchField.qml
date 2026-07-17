import QtQuick
import "." as Controls

Controls.AppTextField {
    Accessible.name: qsTr("Search")
    placeholderText: qsTr("Search")
    inputMethodHints: Qt.ImhNoPredictiveText
}
