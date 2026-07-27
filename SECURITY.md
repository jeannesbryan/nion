# Security

NiOn is a small Tor-routed browser experiment/project, not Tor Browser and not a replacement for Tor Browser's anonymity and anti-fingerprinting design.

## Reporting a security issue

Please avoid publishing a working exploit or sensitive vulnerability details in a public issue before the maintainer has had a reasonable opportunity to review it.

For ordinary bugs, use the repository issue tracker. For a security-sensitive report, use GitHub's private security advisory/reporting feature if it is enabled for the repository.

## Scope

Security-relevant areas include:

- direct-network/proxy bypass;
- DNS or local-network leaks;
- failure to block browsing after Tor failure;
- unsafe external URI handling;
- unsafe profile-file handling;
- bundled Tor/runtime verification;
- AppImage/WebKit subprocess packaging behavior.

See `PRIVACY.md` and `TESTING.md` for current limitations and validation procedures.

## External protocol boundary

Non-web URI schemes can invoke applications outside NiOn. NiOn 1.7.0 requires a direct user gesture and explicit confirmation before supported external schemes are handed to the desktop. `file:`, `javascript:`, `data:`, `blob:`, `about:`, and `nion:` remain non-delegable. A confirmed external application is outside NiOn's Tor-routing guarantee and should be treated as a separate trust boundary.
