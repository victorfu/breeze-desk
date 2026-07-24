#!/usr/bin/env python3
"""Generate deterministic checksums, release metadata, and update-feed hooks."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import urllib.parse
import xml.etree.ElementTree as ET


SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", SPARKLE_NS)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def platform_for(name: str) -> str:
    if name.endswith("-macOS-arm64.dmg"):
        return "macos-arm64"
    if "-Windows-x64-Universal-" in name:
        return "windows-x64-universal"
    if name.endswith("-Windows-x64.msix"):
        return "windows-x64-msix"
    return "supporting-file"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--product-name", required=True)
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--output-directory", required=True, type=pathlib.Path)
    parser.add_argument("--release-notes", type=pathlib.Path)
    parser.add_argument("--require-update-signatures", action="store_true")
    parser.add_argument("artifacts", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("--version must be the three-part numeric CMake project version")
    if not args.product_name.strip():
        parser.error("--product-name must not be empty")
    product_name = args.product_name.strip()
    release_notes = ""
    if args.release_notes is not None:
        if not args.release_notes.is_file():
            parser.error(f"release notes file does not exist: {args.release_notes}")
        release_notes = args.release_notes.read_text(encoding="utf-8").strip()

    output = args.output_directory.resolve()
    output.mkdir(parents=True, exist_ok=True)
    artifacts = sorted((path.resolve() for path in args.artifacts), key=lambda path: path.name)
    missing = [str(path) for path in artifacts if not path.is_file()]
    if missing:
        parser.error(f"missing release artifacts: {', '.join(missing)}")

    entries = []
    checksum_lines = []
    for artifact in artifacts:
        digest = sha256(artifact)
        checksum_lines.append(f"{digest}  {artifact.name}")
        entries.append(
            {
                "fileName": artifact.name,
                "platform": platform_for(artifact.name),
                "size": artifact.stat().st_size,
                "sha256": digest,
                "url": f"{args.base_url.rstrip('/')}/{urllib.parse.quote(artifact.name)}",
            }
        )

    (output / "checksums.txt").write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")
    manifest = {"schemaVersion": 1, "version": args.version, "artifacts": entries}
    (output / "release-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    feed_names = {
        "macos-arm64": "appcast-macos.xml",
        "windows-x64-universal": "appcast-windows-universal.xml",
    }
    for platform, feed_name in feed_names.items():
        platform_items = [
            (entry, artifact)
            for entry, artifact in zip(entries, artifacts)
            if entry["platform"] == platform
        ]
        if not platform_items:
            continue
        rss = ET.Element("rss", {"version": "2.0"})
        channel = ET.SubElement(rss, "channel")
        ET.SubElement(channel, "title").text = f"{product_name} {platform} updates"
        ET.SubElement(channel, "link").text = args.base_url.rstrip("/")
        ET.SubElement(channel, "description").text = f"Signed {product_name} direct-download updates"
        ET.SubElement(channel, "language").text = "en"
        for entry, artifact in platform_items:
            item = ET.SubElement(channel, "item")
            ET.SubElement(item, "title").text = f"{product_name} {args.version}"
            ET.SubElement(item, f"{{{SPARKLE_NS}}}version").text = args.version
            ET.SubElement(item, f"{{{SPARKLE_NS}}}shortVersionString").text = args.version
            ET.SubElement(item, f"{{{SPARKLE_NS}}}os").text = (
                "macos" if platform.startswith("macos-") else "windows"
            )
            if release_notes:
                ET.SubElement(item, "description").text = release_notes
            enclosure = ET.SubElement(
                item,
                "enclosure",
                {
                    "url": entry["url"],
                    "length": str(entry["size"]),
                    "type": "application/octet-stream",
                    f"{{{SPARKLE_NS}}}version": args.version,
                    f"{{{SPARKLE_NS}}}shortVersionString": args.version,
                },
            )
            signature_file = pathlib.Path(str(artifact) + ".edSignature")
            if signature_file.is_file():
                signature_fragment = signature_file.read_text(encoding="utf-8").strip()
                fragment_match = re.search(
                    r'sparkle:edSignature="([A-Za-z0-9+/=_-]+)"', signature_fragment
                )
                if fragment_match:
                    signature = fragment_match.group(1)
                    enclosure.set(f"{{{SPARKLE_NS}}}edSignature", signature)
                elif args.require_update_signatures:
                    parser.error(f"malformed EdDSA update signature sidecar: {signature_file}")
            elif args.require_update_signatures:
                parser.error(f"missing EdDSA update signature sidecar: {signature_file}")
        ET.ElementTree(rss).write(output / feed_name, encoding="utf-8", xml_declaration=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
