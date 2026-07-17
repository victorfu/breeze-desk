# Recording

Press Ctrl/Cmd+Shift+R or choose **Start Recording** to open the recorder. Opening the dialog does not
start capture.

## Record a microphone

1. Select an input device. The list comes from Qt Multimedia and is also available under
   **Settings > Audio > Microphone**.
2. Speak and check the input level before relying on the recording.
3. Choose **Start Recording**. The status and elapsed duration update while capture is active.
4. Use **Pause** and **Resume** as needed. Paused time does not add synthetic samples.
5. Choose **Stop and Save**. BreezeDesk finalizes a local PCM WAV, commits it atomically, and adds it to
   Library.

Enable **Settings > Audio > Transcribe new recordings automatically** to enqueue the new recording after
it is saved. Otherwise, open it in Library and choose **Transcribe** manually. Closing the dialog is
disabled while capture is active; stop the recording first so the WAV header can be finalized.

## Permissions and device errors

On macOS, the first recording can trigger the operating-system microphone prompt. If permission was
denied, enable BreezeDesk in **System Settings > Privacy & Security > Microphone**, then reopen the
recorder. On Windows, check **Privacy & security > Microphone**, confirm desktop apps are allowed, and
verify that the selected device has not been disconnected or reserved by another application.

An empty device list or a capture error is shown in the dialog and no empty Library item is created.
BreezeDesk records microphone input only; it does not capture system audio or provide live streaming
subtitles. For playback-device selection and diagnostics, see [Troubleshooting](troubleshooting.md).
