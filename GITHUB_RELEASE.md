# NiOn 1.2.1 — Version & Dependency Manifest

NiOn 1.2.1 is a maintenance release focused on release integrity rather than new browsing features.

## What changed

- Added centralized release/dependency values under `release/manifest/`.
- Meson now reads the canonical NiOn version file directly.
- About dialog NiOn/Tor/release status values are generated from the manifest.
- AppStream release metadata is generated from a template instead of carrying a separately edited current version.
- AppImage and source-archive filenames are derived from the manifest.
- Bundled Tor fetch, validation, repair, and runtime metadata use the same Tor pins.
- GitHub Actions now accepts `v*` release tags but rejects a tag that does not match the manifest version.
- Release preflight includes a dedicated manifest drift test.
- NiOn 1.2.0 Stage 1–6 browsing features and the persistent profile layout remain unchanged.

## AppImage

The canonical output name for this manifest is:

```text
NiOn-1.2.1-x86_64.AppImage
NiOn-1.2.1-x86_64.AppImage.sha256
```

Verify and run:

```bash
sha256sum -c NiOn-1.2.1-x86_64.AppImage.sha256
chmod +x NiOn-1.2.1-x86_64.AppImage
./NiOn-1.2.1-x86_64.AppImage
```
