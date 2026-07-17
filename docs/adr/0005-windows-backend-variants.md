# ADR 0005: Windows backend variants

Status: Accepted — 2026-07-17

## Context

GGML GPU backends are configure-time choices with different SDKs and runtime DLLs. One Windows executable
that tries to carry Vulkan, CUDA, and CPU increases size, loader failure modes, signing scope, and hardware
assumptions. Every user still needs a CPU recovery path.

## Decision

Publish two x64 NSIS variants from separate build trees:

- Universal: Vulkan preferred worker plus CPU fallback;
- CUDA: CUDA preferred worker plus CPU fallback.

Optionally produce an MSIX from the Universal stage. `WorkerRegistry` chooses by manual override,
platform capability/device detection, installed executable, initialization result, and fallback order.
Selected and actual backend, GPU/device, worker/whisper version, load time, and timings remain visible.

CUDA packaging inspects worker imports and copies only required CUDA runtime DLLs with their EULA and
hash record. A failed preferred backend starts the explicit CPU worker rather than assuming runtime
multi-backend support.

## Consequences

- Release CI must maintain Vulkan and CPU build trees; CUDA needs a provisioned self-hosted runner.
- Installers are larger because CPU fallback is always included, but recovery is deterministic.
- Worker filenames/layout and signing must stay synchronized with `WorkerRegistry` and package docs.
- A compiled backend is not reported as hardware-tested unless inference ran on that device.

## Rejected alternatives

A single combined worker couples unrelated toolkits and makes fallback ambiguous. CPU-only distribution
would discard expected Windows GPU acceleration. Downloading backend binaries after install would expand
the privacy/network and code-signing boundary.
