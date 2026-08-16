import QtQuick
import QtQuick.Window

Window {
    id: root

    readonly property bool screenshotMode: cfgScreenshot.length > 0
    readonly property bool pinSize: screenshotMode || cfgOffscreen

    width: pinSize ? 1404 : Screen.width
    height: pinSize ? 1872 : Screen.height
    visible: true
    title: "Apps"
    color: "white"

    property int secondsLeft: cfgTimeoutSeconds

    Rectangle {
        anchors.fill: parent
        color: "white"
    }

    // ---------------------------------------------------------------- header
    Rectangle {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 150
        color: "black"

        Text {
            anchors { left: parent.left; leftMargin: 50; verticalCenter: parent.verticalCenter }
            color: "white"
            font.pixelSize: 48
            font.bold: true
            text: "Apps"
        }

        Text {
            anchors { right: parent.right; rightMargin: 50; verticalCenter: parent.verticalCenter }
            color: "white"
            font.pixelSize: 26
            text: "back to tablet in " + root.secondsLeft + " s"
        }
    }

    // -------------------------------------------------------------- app list
    Column {
        anchors { top: header.bottom; topMargin: 60; horizontalCenter: parent.horizontalCenter }
        spacing: 40

        Repeater {
            model: cfgApps

            delegate: Rectangle {
                required property var modelData

                width: root.width - 160
                height: 190
                color: "white"
                border.color: "black"
                border.width: 5
                radius: 14

                Column {
                    anchors { left: parent.left; leftMargin: 40; verticalCenter: parent.verticalCenter }
                    spacing: 10

                    Text {
                        font.pixelSize: 44
                        font.bold: true
                        text: modelData.name
                    }
                    Text {
                        font.pixelSize: 26
                        color: "#505050"
                        text: modelData.description
                        visible: modelData.description.length > 0
                    }
                }

                Text {
                    anchors { right: parent.right; rightMargin: 40; verticalCenter: parent.verticalCenter }
                    font.pixelSize: 60
                    text: "›"   // ›
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: chooser.choose(modelData.exec)
                }
            }
        }
    }

    // ------------------------------------------------------- back to tablet
    Rectangle {
        anchors { bottom: parent.bottom; bottomMargin: 80; horizontalCenter: parent.horizontalCenter }
        width: root.width - 160
        height: 190
        color: "black"
        radius: 14

        Text {
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 44
            font.bold: true
            text: "Back to tablet"
        }

        MouseArea {
            anchors.fill: parent
            onClicked: chooser.choose("tablet")
        }
    }

    // If nobody chooses, fall back to the tablet UI rather than sitting here.
    Timer {
        interval: 1000
        repeat: true
        running: !root.screenshotMode
        onTriggered: {
            root.secondsLeft--;
            if (root.secondsLeft <= 0) {
                chooser.choose("tablet");
            }
        }
    }

    // ------------------------------------------------------------ screenshot
    Timer {
        running: root.screenshotMode
        interval: 800
        onTriggered: {
            root.contentItem.grabToImage(function (result) {
                result.saveToFile(cfgScreenshot);
                Qt.quit();
            });
        }
    }
}
