import QtQuick

Item {
    id: root
    signal importTriggered
    signal recordingTriggered
    signal playPauseTriggered
    signal searchTriggered
    signal saveTriggered
    signal exportTriggered
    signal undoTriggered
    signal redoTriggered
    signal settingsTriggered
    Shortcut { sequences: ["Ctrl+O", "Meta+O"]; onActivated: root.importTriggered() }
    Shortcut { sequence: "Ctrl+Shift+R"; onActivated: root.recordingTriggered() }
    Shortcut { sequence: "Meta+Shift+R"; onActivated: root.recordingTriggered() }
    Shortcut { sequence: "Space"; onActivated: root.playPauseTriggered() }
    Shortcut { sequences: ["Ctrl+F", "Meta+F"]; onActivated: root.searchTriggered() }
    Shortcut { sequences: ["Ctrl+S", "Meta+S"]; onActivated: root.saveTriggered() }
    Shortcut { sequence: "Ctrl+E"; onActivated: root.exportTriggered() }
    Shortcut { sequence: "Meta+E"; onActivated: root.exportTriggered() }
    Shortcut { sequences: ["Ctrl+Z", "Meta+Z"]; onActivated: root.undoTriggered() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Meta+Shift+Z"]; onActivated: root.redoTriggered() }
    Shortcut { sequence: "Ctrl+,"; onActivated: root.settingsTriggered() }
    Shortcut { sequence: "Meta+,"; onActivated: root.settingsTriggered() }
}
