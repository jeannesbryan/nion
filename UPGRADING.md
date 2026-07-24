# Upgrading NiOn

NiOn keeps its persistent browser profile outside the AppImage. The profile paths are intentionally version-independent:

```text
~/.local/share/nion/
~/.config/nion/
~/.cache/nion/
```

## Normal AppImage upgrade

1. Close the old NiOn instance normally.
2. Keep the profile directories above.
3. Replace the old AppImage with the new AppImage.
4. Make it executable if necessary.
5. Start the new AppImage.

Example:

```bash
chmod +x NiOn-1.0.0-x86_64.AppImage
./NiOn-1.0.0-x86_64.AppImage
```

Do **not** copy profile data into the AppImage or AppDir.

## What should survive an upgrade

When the profile is healthy, NiOn is designed to preserve:

- cookies and website data;
- website login sessions where the website itself continues accepting them;
- preferences;
- saved tabs/session state;
- download history;
- Tor client state.

## Recovery behavior

NiOn bounds and validates its small profile metadata files. A malformed file may be moved aside with a suffix similar to:

```text
session.ini.corrupt-20260725-231500-...
```

An obviously invalid `cookies.sqlite` may also be quarantined. This can cause website logout only when the cookie database itself is already unusable.

Tor cache/state recovery uses a separate quarantine path and is designed not to destroy unrelated browser profile data.

## Downgrades

Downgrading is not a primary compatibility target. Before downgrading across major profile-format changes, back up:

```text
~/.local/share/nion/
~/.config/nion/
```

NiOn 1.0.0 uses session format 1 inherited from the 0.x series, so the 0.11.0 → 1.0.0 transition does not intentionally introduce a new profile format.
