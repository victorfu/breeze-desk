# ADR 0007: Windows distribution uses Microsoft Store MSIX

Status: Accepted — 2026-07-27

Amended: 2026-07-29 — attach the unsigned Store-submission MSIX to tagged GitHub Releases for
maintainer access, with an explicit non-installable warning.

## Context

The original Windows release path produced an NSIS installer with WinSparkle updates and optionally
created an MSIX from the same staging directory. Maintaining two Windows channels required separate
signing credentials, update feeds, installer behavior, and release artifacts. Neither channel had been
published, and the intended Windows launch channel is Microsoft Store.

## Decision

Windows releases produce one x64 MSIX containing the Vulkan preferred ASR worker and CPU fallback. CI
builds the unsigned package with the identity assigned by Partner Center, retains it as a workflow
artifact, and attaches it to the matching GitHub Release for maintainer access. The release notes clearly
identify it as an un-installable Partner Center submission package. A maintainer uploads that package
manually to Partner Center; Microsoft Store certification, signing, distribution, and updates own the
public delivery path.

The NSIS installer, WinSparkle adapter and tooling, Windows appcast, Authenticode release secrets, and
direct-download Windows artifacts are removed. Windows builds reject `BREEZEDESK_ENABLE_UPDATES=ON`.
Local MSIX installation tests use a self-signed development certificate that is trusted only on the
test machine and is never used for public distribution.

## Consequences

- The public Partner Center identity name, publisher, and publisher display name are versioned with the
  packaging source; Windows release CI needs no identity variable or signing secret.
- The unsigned MSIX is attached to GitHub Releases for maintainer access, with a prominent warning that
  users cannot install it directly and must use the Microsoft Store build.
- Windows updates and rollback are controlled by Microsoft Store rather than application code.
- Partner Center certification must approve the `runFullTrust` capability declared by the desktop app.
- A signed local installation smoke test and a real Partner Center submission remain mandatory release
  checks; successful `makeappx` output alone is insufficient.
