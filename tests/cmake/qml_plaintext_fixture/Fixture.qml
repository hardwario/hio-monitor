// Fixture of qml_plaintext_check_detects (ADR-0184): exactly five problems, marked BAD.
import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string deviceText: ""

    Label {
        text: qsTr("A translated literal")
    }
    Label {
        text: "A plain literal"
    }
    Label {
        textFormat: Text.PlainText
        text: root.deviceText
    }
    Label {
        // BAD: data without textFormat
        text: root.deviceText
    }
    Text {
        // BAD: an .arg() continuation is not a literal
        text: qsTr("Name: %1")
              .arg(root.deviceText)
    }
    TextArea {
        // BAD: rich text outside the allow-list
        textFormat: TextEdit.RichText
        text: root.deviceText
    }
    ListModel {
        ListElement {
            text: "not a label"
        }
    }
    TextField {
        text: root.deviceText
    }
    Button {
        // BAD: the attached ToolTip renders AutoText
        ToolTip.text: root.deviceText
    }
    ToolTip {
        // BAD: a plain ToolTip element (use HToolTip)
        text: root.deviceText
    }
    function f() {
        const o = { text: root.deviceText };
        return o;
    }
}
