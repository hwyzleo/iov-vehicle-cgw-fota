#!/usr/bin/env python3
# ci/verify-install-manifest.py - CGW-FOTA-DSN-CR-008 §12.3/§12.5/§验收测试
#
# Verify the cgw-fota-runtime install component produces an exact, stable file
# set under a DESTDIR install-root, with no out-of-bounds / stale / dev files.
#
# Usage:
#   verify-install-manifest.py <install-root> [--prefix /usr]
#
# Exit code: 0 = pass, 1 = manifest mismatch (extra/missing/forbidden files).
#
# Expected FHS layout (CMAKE_INSTALL_PREFIX=/usr, CR-008 §12.3 staging):
#   <install-root>/usr/bin/cgw-fota
#   <install-root>/usr/lib/systemd/system/cgw-fota.service
#   <install-root>/etc/cgw/conf.d/fota.yaml
#   <install-root>/usr/share/cgw-fota/schema/fota.schema.yaml

import argparse
import os
import sys

# Exact expected relative paths for the cgw-fota-runtime component.
EXPECTED = {
    "usr/bin/cgw-fota",
    "usr/lib/systemd/system/cgw-fota.service",
    "etc/cgw/conf.d/fota.yaml",
    "usr/share/cgw-fota/schema/fota.schema.yaml",
}

# File basenames that MUST NEVER appear in the runtime install (CR-008 §12.3:
# dev fixtures, tests, source private headers, build tree, lib archive).
FORBIDDEN_PATTERNS = (
    "CgwFotaTests",
    "libcgw_fota_lib",
    "fota_config.yaml",   # dev fixture
    "common.yaml",        # dev fixture (CGW-BUILD owns platform common.yaml)
    "fota.default.yaml",  # installed renamed as fota.yaml; original must not ship
    "compile_commands.json",
    ".o",
    ".a",
)


def collect_files(root):
    found = set()
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            found.add(rel)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("install_root", help="DESTDIR install-root to verify")
    args = ap.parse_args()

    root = args.install_root
    if not os.path.isdir(root):
        print(f"[FAIL] install-root not found: {root}", file=sys.stderr)
        return 1

    found = collect_files(root)

    missing = EXPECTED - found
    extra = found - EXPECTED

    # Forbidden content scan over every found path.
    forbidden = sorted(
        rel for rel in found
        if any(pat in os.path.basename(rel) or rel.endswith(pat)
               for pat in FORBIDDEN_PATTERNS)
    )

    ok = True

    if missing:
        ok = False
        print("[FAIL] missing expected files:", file=sys.stderr)
        for m in sorted(missing):
            print(f"       - {m}", file=sys.stderr)

    if extra:
        ok = False
        print("[FAIL] unexpected extra files (out-of-bounds / stale):",
              file=sys.stderr)
        for e in sorted(extra):
            print(f"       + {e}", file=sys.stderr)

    if forbidden:
        ok = False
        print("[FAIL] forbidden content in runtime component:", file=sys.stderr)
        for f in forbidden:
            print(f"       ! {f}", file=sys.stderr)

    if ok:
        print(f"[OK] cgw-fota-runtime install manifest exact: "
              f"{len(found)} file(s) under {root}")
        for rel in sorted(EXPECTED):
            print(f"     - {rel}")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
