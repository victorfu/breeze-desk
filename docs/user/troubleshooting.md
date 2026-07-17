# Troubleshooting

Start with **Settings > Diagnostics > Refresh**. The sanitized export records app/Qt/OS, worker protocol
and runtime state, selected/actual backend, FFmpeg detection, storage locations when explicitly included,
and sanitized settings. Check the selected model card separately for checksum state. Prefer **Export
Sanitized Diagnostics** over screenshots containing local paths or transcript text.

| Symptom | Action |
| --- | --- |
| Model not installed | Install Q5/Q8 in Models, wait for **Verified**, then retry. |
| Checksum mismatch | Verify or redownload. A corrupt `.part`, installed model, or custom-model sidecar is never loaded. |
| Model load failed | Run **Test Model**, confirm available memory and backend, then try Q5 or CPU. |
| GPU initialization failed | Select CPU, or use the Windows installer matching Vulkan/CUDA and update the GPU driver. |
| Out of memory | Use Q5, close memory-heavy applications, switch backend, and retry the interrupted job. |
| Unsupported/missing media | Relink the source and confirm its extension is supported. Reference imports break when moved. |
| Audio decode failed | Confirm bundled FFmpeg/FFprobe versions, free disk space, and read access to the source. |
| Job is Interrupted | Wait for worker recovery and choose Resume; completed chunks should not run again. |
| Worker repeatedly exits | Stop retrying, choose CPU/Q5, export diagnostics, and retain the durable job id. |
| Export failed | Select a writable directory, check free space, and confirm no other program locks the target. |
| Microphone unavailable | Re-enable OS permission, reconnect the device, and reopen the recorder. |
| Search results are incomplete | Clear the current review/low-confidence filter and verify the active transcript revision. |

## Database startup errors

Do not delete or edit the SQLite file manually. A migration is transactional and creates a backup before
upgrading an existing schema; startup also runs `PRAGMA quick_check`. If integrity or migration checksum
validation fails, stop creating new work, preserve the reported database path, and create **Storage >
Database backup** if the application permits it. Share only sanitized diagnostics through the project's
private security/support channel.

## Privacy-safe reporting

Diagnostics excludes audio, transcript text, glossary content, and personal paths by default. Never post
model files, signing credentials, raw logs with unredacted paths, or private recordings in a public issue.
The local-processing and network guarantees are listed in [Privacy](privacy.md).
