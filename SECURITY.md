# Security Policy

Security reports must be submitted through GitHub's private vulnerability reporting form at
<https://github.com/victorfu/breeze-desk/security/advisories/new>, not filed as public issues. If that
form is unavailable, contact the repository owner through the private contact method on
<https://github.com/victorfu>. Include the affected version, platform, reproduction steps, and
sanitized diagnostics. Never attach private audio, transcripts, glossary data, model files,
credentials, signing material, or unredacted local paths.

Supported releases receive fixes on the latest `1.x` branch. BreezeDesk intentionally makes no network
request except explicit model downloads and enabled update checks. Reports of any other outbound traffic,
unsafe local socket access, path traversal, model-checksum bypass, or unintended diagnostic disclosure
are treated as security issues.
