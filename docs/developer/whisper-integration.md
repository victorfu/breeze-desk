# whisper.cpp integration

The worker links the `whisper` target from v1.9.1 commit
`f049fff95a089aa9969deb009cdd4892b3e74916`. It never launches a whisper executable. `WhisperContext` and
`WhisperVadContext` use `unique_ptr` custom deleters. A `WhisperModelSession` is confined to one inference
QThread and serves one transcription at a time while the worker main thread continues IPC.

Both context wrappers fail closed unless the request supplies a trusted SHA-256. They stream and compare
the model on the inference thread before `whisper_init_from_file_with_params` or
`whisper_vad_init_from_file_with_params`. VAD-enabled transcription rechecks the VAD file immediately
before `whisper_full`, including when the separate streaming-analysis context is already cached.

Segment t0/t1 values are centiseconds and convert to milliseconds by multiplying by ten before adding
the chunk offset. New-segment callbacks consume every segment represented by `n_new`. Progress is made
monotonic and terminal completion sets 100. Prompt budgeting uses exported `whisper_tokenize` in two
passes and caps content at `whisper_n_text_ctx()/2`.

VAD state is retained with `whisper_vad_detect_speech_no_reset`, but its probability buffer is copied
after each 512-sample-aligned block before the next call overwrites it. A BreezeDesk cross-block state
machine produces speech regions. `whisper_get_timings()` ownership is copied and released by the adapter.
Backend reporting combines the selected worker variant, GGML device enumeration, and captured init logs.

## Adapter layers

- `WhisperContext` and `WhisperVadContext` own C pointers with custom deleters.
- `WhisperModelSession` serializes model load/unload and one active transcription.
- `WhisperParameterMapper` maps `PresetRegistry` plus job settings into `whisper_full_params`.
- `WhisperSegmentCollector` converts callbacks and final C API queries into domain segments.
- `WhisperLogBridge` redirects library logging into a Qt category without token-level Release noise.
- `WhisperBackendInfo` records runtime version, system/device information, actual backend, and timings.

The integration calls the public APIs for default context/full/VAD parameters, model and VAD loading,
`whisper_full`, segment/token/timestamp/probability access, tokenization/context sizing, timings, system
information, version, logging, streaming VAD/reset/free, progress, new-segment, and abort callbacks.
Feature code does not include whisper headers outside the ASR target.

## Load and fallback

`ModelLoadOptions` carries path, expected SHA-256, requested backend, GPU device, and flash-attention flag.
Hashing occurs on the inference thread before native parsing. Context parameters enable the requested GPU
when compiled into that worker. A failed GPU initialization may create a CPU context, but the result must
set requested/actual backend, `usedFallback`, flash-attention result, load time, runtime version, and
system info so the GUI can disclose it.

Windows Universal installs separate Vulkan and CPU workers. macOS ships the
Metal+CPU arm64 worker. `WorkerRegistry` selects a compatible executable before context initialization;
the C API adapter never assumes one binary contains every backend.

## Full parameters and callbacks

The mapper always keeps `translate=false` and timestamps enabled. Job settings control language,
context/carry prompt, token timestamps, thread count, blank/non-speech suppression, no-speech threshold,
temperature fallback, VAD model/thresholds, and low-confidence threshold. Presets are centralized:

| Preset | Strategy | Key values |
| --- | --- | --- |
| Fast | Greedy | `best_of=1`, no context, no temperature increment. |
| Balanced | Greedy | `best_of=2`, context enabled, temperature increment 0.2. |
| Accurate | Beam | `beam_size=5`, context enabled, temperature increment 0.2. |

`progress_callback` emits monotonic integer progress. `new_segment_callback` consumes all `n_new`
segments. `abort_callback` reads an `std::atomic_bool`; cancellation never terminates the QThread.
Confidence is aggregated from token probabilities and stored with no-speech probability and the derived
low-confidence flag.

Protocol and checksum fields are specified in [worker-protocol.md](worker-protocol.md); chunk-level memory
and timestamp rules are in [long-form-transcription.md](long-form-transcription.md).
