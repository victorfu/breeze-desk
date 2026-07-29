# Getting started

BreezeDesk needs no account and performs transcription on this computer. The first time transcription
needs a model, BreezeDesk automatically downloads and verifies the recommended Q5 model. This is a
one-time download of about 1 GB; the recording stays on this computer. Q8 and custom models remain
available from **Settings > Transcription > Manage Models**.

## Install on Windows

Use the Microsoft Store build when it is available; it is certified and updates automatically. For a
direct download, get `BreezeDesk-<version>-Windows-x64-portable.zip` from the matching GitHub Release,
extract its versioned folder, and run `bin\BreezeDesk.exe`. No installation is required. Because the
portable build is unsigned, Microsoft Defender SmartScreen may show **Windows protected your PC** on
first launch; choose **More info**, then **Run anyway**. You can verify the download against the
published `.sha256` file.

“Portable” describes the no-install application, not its data. Settings, the database, and downloaded
models remain under the Windows application-data location shown in Settings, so replacing an extracted
folder during an upgrade does not delete them. The Store and portable builds use separate data stores
because of MSIX filesystem virtualization. Switching channels does not migrate the library, settings,
or models between them.

## Create the first transcript

1. Optionally add important names under **Name Dictionary**. Enabled names are included when a new
   transcription starts.
2. Choose **Import Files**, drag local media into Library, or press Ctrl/Cmd+O. Use the Library's more
   menu and choose **Import Folder…** to scan a folder with cancellable progress.
3. New media starts transcribing automatically by default. You can keep browsing the Library while the
   recommended model downloads and the work runs. Choose **Activity** in the top bar to see progress,
   cancel, retry, or resume work.
4. Select a recording card to play the source and view its latest transcript. If automatic transcription
   is disabled, choose **Start Transcription**. Choose **Transcribe Again…** to replace an existing
   transcript after the new result is ready.
5. For a recording without a completed transcript, partial segments can appear as units complete.
   Editing stays locked during processing, then unlocks for correction, review, playback, and export.

The normal defaults are Balanced quality, Chinese recognition, automatic backend selection, and Silero
VAD. Technical tuning is available under **Settings > Transcription > Show Advanced**. See
[Models](models.md) for model verification and custom-model handling.

## Long jobs and application exit

The top-bar **Activity** view shows the current stage, progress, and retry/cancel controls. Cancelling retains durable completed
chunks and diagnostics. An unexpected worker exit marks the job **Interrupted**; after the worker
restarts, **Resume** continues with the first incomplete chunk rather than repeating finished work.

Closing the window may hide it in the tray when **Settings > General > Close behavior** is set to
**Minimize to tray**. Choosing Quit stops the helper safely and leaves an active job resumable. Check
**Activity** before shutting down a machine during a long job.

## Files and privacy

BreezeDesk stores its database, managed media, normalized cache, waveform data, models, logs, and exports
under the platform application-data location shown in Settings. An import can reference the original or
copy it into managed storage. Trash and permanent deletion never remove an original source outside
BreezeDesk's managed directories.

To recover or permanently remove an item, open the Library's more menu and choose **Trash**.

Continue with [Importing media](importing-media.md), [Transcription](transcription.md), and
[Editing transcripts](editing.md). The exact network boundary is documented in [Privacy](privacy.md).
