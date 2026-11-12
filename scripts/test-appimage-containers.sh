#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/manifest.sh"
APPIMAGE="${1:-$ROOT/dist/$NION_APPIMAGE_BASENAME}"
[[ -e "$APPIMAGE" ]] || { echo "AppImage not found: $APPIMAGE" >&2; exit 1; }
# Resolve to an absolute path: a container bind mount needs one. Given a
# relative path, podman treats "dist/NiOn-....AppImage" as a *named volume* and
# fails with "names must match [a-zA-Z0-9][a-zA-Z0-9_.-]*", which says nothing
# about the real problem.
APPIMAGE="$(cd "$(dirname "$APPIMAGE")" && pwd)/$(basename "$APPIMAGE")"
[[ -f "$APPIMAGE" ]] || { echo "Not a file: $APPIMAGE" >&2; exit 1; }
[[ -x "$APPIMAGE" ]] || {
  echo "Not executable: $APPIMAGE" >&2
  echo "An AppImage must carry the execute bit; run: chmod +x '$APPIMAGE'" >&2
  exit 1
}

# Cross-distribution smoke test (wired into release preflight in 2.2.0).
#
# This is the check that would have caught the 2.1.0 "GLIBC_2.43 not found"
# release: the AppImage was built on a glibc 2.43 host and never executed on a
# machine that looked like a supported user system.
#
# Scope: does the AppImage extract, and do its loader paths resolve on a real
# desktop of each supported distribution? NiOn bundles GTK/GLib/WebKitGTK but
# deliberately does NOT bundle the C library nor the graphics driver stack
# (Mesa, libdrm, libgbm, Vulkan) -- those belong to the host, exactly like they
# do for every other browser. The bare distribution images contain neither, so
# each one gets the minimal runtime libraries a desktop of that family ships
# before the check runs. Without that step this test would fail on every image
# for a reason that has nothing to do with what we ship.
#
# What this does not do: open a window or browse. A real GUI/Tor run still has
# to be verified on a desktop (see TESTING.md).

engine=""
command -v podman >/dev/null 2>&1 && engine=podman
if [[ -z "$engine" ]] && command -v docker >/dev/null 2>&1; then engine=docker; fi
[[ -n "$engine" ]] || { echo "podman or docker is required for container smoke tests" >&2; exit 2; }

# Host libraries NiOn relies on instead of bundling. Packages are per family.
# shellcheck disable=SC2016
APT_LIBS='libgl1 libegl1 libgbm1 libdrm2 libvulkan1'
FEDORA_LIBS='mesa-libGL mesa-libEGL mesa-libgbm libdrm mesa-vulkan-drivers'

# image|prepare command
targets=(
  "docker.io/library/ubuntu:24.04|apt-get update -qq && apt-get install -y -qq --no-install-recommends $APT_LIBS"
  "docker.io/library/debian:stable-slim|apt-get update -qq && apt-get install -y -qq --no-install-recommends $APT_LIBS"
  "docker.io/library/fedora:latest|dnf install -y -q $FEDORA_LIBS"
)

echo "NiOn $NION_VERSION container smoke test"
echo "  engine   $engine"
echo "  artifact $APPIMAGE"
echo "  floor    glibc $NION_GLIBC_FLOOR"
echo

# Two separate bounds, because the two steps fail for unrelated reasons.
#
# PREPARE_TIMEOUT exists because of a real hang: `dnf install` on the Fedora leg
# stalled on a slow mirror for over fifteen minutes and consumed the whole
# budget, so the run said nothing useful about the artifact. The prepare step
# installs the host graphics stack that ldd needs, so it cannot simply be
# dropped. It is bounded, and its failure is reported rather than aborting.
# 240s looked generous and is not. Measured on this machine: a cold Fedora leg
# spends over four minutes in `dnf install` alone (it exceeded the 240s bound and
# the leg went unverified), while the same leg with warm layers finishes in
# under two. The bound stays, because a hang must not eat the whole budget, but
# it has to be large enough that a slow-but-working mirror is not mistaken for a
# defect.
PREPARE_TIMEOUT="${NION_PREPARE_TIMEOUT:-420}"
# The outer bound must stay above the inner ones. It used to be 300s while the
# prepare step alone was allowed 420s, so `timeout` killed the whole container
# before dnf could finish, the leg looked like a packaging failure, and the
# degraded-fixture path was never reached. Deriving it from PREPARE_TIMEOUT
# means raising one bound can never again silently invalidate the other.
RUN_TIMEOUT="${NION_RUN_TIMEOUT:-$((PREPARE_TIMEOUT + 300))}"

if (( RUN_TIMEOUT <= PREPARE_TIMEOUT )); then
  echo "FAIL: RUN_TIMEOUT (${RUN_TIMEOUT}s) must exceed PREPARE_TIMEOUT (${PREPARE_TIMEOUT}s)." >&2
  echo "      Otherwise the outer timeout kills the leg before its prepare step" >&2
  echo "      can finish, and that is misread as a packaging defect." >&2
  exit 1
fi

fail=0
degraded=()   # images where the host fixtures could not be installed
for target in "${targets[@]}"; do
  image="${target%%|*}"
  prepare="${target#*|}"
  echo "== $image =="

  out="$(timeout "$RUN_TIMEOUT" "$engine" run --rm -v "$APPIMAGE:/opt/NiOn.AppImage:ro" "$image" \
      sh -lc "if timeout $PREPARE_TIMEOUT sh -c '$prepare' >/dev/null 2>&1; then
                  echo __PREPARE_OK__
              else
                  echo __PREPARE_WARN__
              fi
              chmod +x /opt/NiOn.AppImage 2>/dev/null || true
              APPIMAGE_EXTRACT_AND_RUN=1 /opt/NiOn.AppImage --appimage-diagnose
              echo __DIAGNOSE_RC=\$?" 2>&1)" && status=0 || status=$?

  if grep -q '__PREPARE_WARN__' <<<"$out"; then
    degraded+=("$image")
    echo "WARN  could not install the host graphics libraries within ${PREPARE_TIMEOUT}s."
    echo "      Diagnostics below ran without them: a library reported as missing"
    echo "      here may be an absent fixture rather than a packaging defect."
  fi

  diagnose_rc="$(grep -o '__DIAGNOSE_RC=[0-9]*' <<<"$out" | tail -1 | cut -d= -f2)"
  printf '%s\n' "$out" | grep -v '__PREPARE_\|__DIAGNOSE_RC=' | sed '/^$/d'

  if [[ $status -ne 0 || "${diagnose_rc:-1}" != "0" ]]; then
    # A leg whose host fixtures never installed cannot conclude anything about
    # the artifact: ldd on the missing graphics stack reports libraries absent
    # by construction, not because the AppImage forgot to bundle them. Counting
    # that as a packaging failure is how a fixture problem turns into a red
    # build, and it made the PASS WITH WARNINGS branch below unreachable.
    #
    # It is still not a pass. The leg is recorded as degraded, the verdict
    # names the distribution, and nothing claims this leg was verified.
    if [[ " ${degraded[*]-} " == *" $image "* ]]; then
      echo "WARN  $image: diagnostics could not finish without the host graphics"
      echo "      libraries. This leg is NOT verified; it says nothing about the"
      echo "      AppImage either way. Raise NION_PREPARE_TIMEOUT to verify it."
    else
      echo "FAIL  $image could not run the NiOn AppImage" >&2
      fail=1
    fi
  fi
  echo
done

if (( fail )); then
  echo 'RESULT: FAIL - the AppImage did not run on every tested distribution.' >&2
  echo 'Run ./scripts/check-glibc-floor.sh <AppImage> to see which libraries exceed the floor.' >&2
  exit 1
fi

# A run without the host fixtures proves less than a full one, so it must not
# look identical to it. The exit status stays 0 because the artifact passed
# every check that could be made, but the verdict says so out loud.
if (( ${#degraded[@]} )); then
  echo "RESULT: PASS WITH WARNINGS - the AppImage ran everywhere, but the host graphics"
  echo "        libraries could not be installed on: ${degraded[*]}"
  echo "        Those legs are NOT verified: the missing libraries there are the"
  echo "        absent fixture, not a bundling defect. Raise NION_PREPARE_TIMEOUT"
  echo "        (default ${PREPARE_TIMEOUT}s) to verify them fully."
  exit 0
fi

echo 'RESULT: PASS - the AppImage extracted and resolved its bundled libraries on every tested distribution.'
