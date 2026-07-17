# Getting started

BreezeDesk needs no account and performs transcription on this computer. Before the first job, open
**Models** and install an ASR model. Q5 is the recommended memory/speed balance; Q8 uses more memory. A
download is not usable until its exact size and SHA-256 state is **Verified**. Packaged media-tool
availability is checked when preparation starts and can be refreshed in Diagnostics.

## Create the first transcript

1. Choose **Import Files**, drag local media into Library, or press Ctrl/Cmd+O. **Open Folder** scans a
   selected folder for supported media and shows cancellable progress.
2. Select the recording card. The recording view loads the source into the player and shows any existing
   transcript revision.
3. Review **Settings > Transcription**. Balanced, language `zh`, automatic backend selection, and Silero
   VAD are the normal starting point for a long Taiwan Mandarin meeting.
4. Optionally select a profile on Glossary and maintain its project context, then choose **Transcribe**.
   The selected profile is snapshotted into the job. The job appears in Queue; normalization and inference
   run outside the UI thread.
5. Partial segments appear as units complete. Editing stays locked while the active revision is being
   written, then unlocks for correction, review, playback, and export.

If the model is absent, corrupt, or already being removed, transcription remains disabled and Models
shows the action to take. See [Models](models.md) for download and custom-model handling.

## Long jobs and application exit

Queue shows the current stage, progress, and retry/cancel controls. Cancelling retains durable completed
chunks and diagnostics. An unexpected worker exit marks the job **Interrupted**; after the worker
restarts, **Resume** continues with the first incomplete chunk rather than repeating finished work.

Closing the window may hide it in the tray when **Settings > General > Close behavior** is set to
**Minimize to tray**. Choosing Quit stops the helper safely and leaves an active job resumable. Always
check Queue before shutting down a machine during a long job.

## Files and privacy

BreezeDesk stores its database, managed media, normalized cache, waveform data, models, logs, and exports
under the platform application-data location shown in Settings. An import can reference the original or
copy it into managed storage. Trash and permanent deletion never remove an original source outside
BreezeDesk's managed directories.

Continue with [Importing media](importing-media.md), [Transcription](transcription.md), and
[Editing transcripts](editing.md). The exact network boundary is documented in [Privacy](privacy.md).
