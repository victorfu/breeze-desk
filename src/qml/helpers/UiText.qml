pragma Singleton

import QtQuick

QtObject {
    id: root

    function jobState(value) {
        switch (String(value)) {
        case "Queued": return qsTr("Queued")
        case "Preparing": return qsTr("Preparing")
        case "Normalizing": return qsTr("Normalizing audio")
        case "WaitingForModel": return qsTr("Waiting for model")
        case "LoadingModel": return qsTr("Loading model")
        case "AnalyzingSpeech": return qsTr("Analyzing speech")
        case "Transcribing": return qsTr("Transcribing")
        case "Finalizing": return qsTr("Finalizing")
        case "Completed": return qsTr("Completed")
        case "Cancelling": return qsTr("Cancelling")
        case "Cancelled": return qsTr("Cancelled")
        case "Failed": return qsTr("Failed")
        case "Interrupted": return qsTr("Interrupted")
        default: return String(value)
        }
    }

    function jobStage(value) {
        switch (String(value)) {
        case "InspectingMedia": return qsTr("Inspecting media")
        case "NormalizingAudio": return qsTr("Normalizing audio")
        default: return root.jobState(value)
        }
    }

    function recordingStatus(value) {
        return String(value) === "Imported" ? qsTr("Imported") : root.jobState(value)
    }

    function shortDateTime(value) {
        const locale = Qt.locale(qsTr("en_US", "UI locale used for date and time formatting"))
        return locale.toString(value, Locale.ShortFormat)
    }

    function modelState(value) {
        switch (String(value)) {
        case "NotInstalled": return qsTr("Not installed")
        case "Requested": return qsTr("Preparing download")
        case "Downloading": return qsTr("Downloading")
        case "Paused": return qsTr("Paused")
        case "Verifying": return qsTr("Verifying")
        case "Installed": return qsTr("Installed")
        case "Cancelled": return qsTr("Cancelled")
        case "Failed": return qsTr("Failed")
        case "Testing": return qsTr("Testing")
        case "Loaded": return qsTr("Loaded")
        default: return String(value)
        }
    }

    function modelDescription(modelId, fallback) {
        switch (String(modelId)) {
        case "breeze-asr-25-q5":
            return qsTr("Recommended offline model for Apple Silicon and systems with 8 GB memory.")
        case "breeze-asr-25-q8":
            return qsTr("Higher quality mode for systems with more available memory.")
        case "silero-vad-v6.2.0":
            return qsTr("Speech activity model used to place long-form chunk boundaries in silence.")
        default:
            return String(fallback)
        }
    }
}
