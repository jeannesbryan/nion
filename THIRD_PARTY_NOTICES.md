# Third-party notices

NiOn is developed independently and is **not** Tor Browser and is not affiliated with or endorsed by the Tor Project, WebKit, GNOME, or the AppImage project.

NiOn itself is licensed under GPL-3.0-or-later. A built AppImage may also contain third-party runtime components. Those components remain under their own licenses.

## Tor

NiOn uses the Tor Expert Bundle as its bundled Tor runtime.

- Project: Tor
- Upstream: https://www.torproject.org/
- Runtime source/download information: https://www.torproject.org/download/tor/
- NiOn 1.0.0 pin: Tor Browser Expert Bundle 15.0.19 / Tor 0.4.9.11 for GNU/Linux x86_64

Tor and Tor Project trademarks remain the property of their respective owners.

## GTK / GLib

NiOn uses GTK 4 and GLib from the GNOME project.

- GTK: https://www.gtk.org/
- GLib: https://gitlab.gnome.org/GNOME/glib

These libraries are distributed under their respective upstream licenses, including LGPL terms.

## WebKitGTK

NiOn uses WebKitGTK 6 as its web engine.

- Upstream: https://webkitgtk.org/

WebKitGTK and the libraries it depends on remain under their respective upstream licenses.

## libsoup

NiOn links against libsoup 3.

- Upstream: https://libsoup.gnome.org/

## AppImage tools

NiOn's packaging scripts use AppImage tooling to produce the AppImage.

- AppImage documentation: https://docs.appimage.org/
- appimagetool: https://github.com/AppImage/appimagetool

## Build-host dependency set

The AppImage builder recursively deploys practical user-space ELF dependencies from the build host while intentionally leaving host-core components such as glibc and graphics-driver stacks to the target system. The exact bundled library versions therefore depend on the machine/runner used to build a specific AppImage.

For release troubleshooting, the AppImage contains `usr/lib/nion/BUILD-INFO` with the main toolkit/runtime versions observed at build time.

Distributors are responsible for preserving all license/copyright obligations of the exact third-party binaries they redistribute.
