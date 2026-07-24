# Runtime directory

Run:

```bash
./scripts/fetch-tor-runtime.sh
```

to create `runtime/tor/` from the signed official Tor Expert Bundle.

The fetched third-party runtime is intentionally excluded from source control; the build/AppImage scripts copy it into packaged NiOn artifacts.
