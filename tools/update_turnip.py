#!/usr/bin/env python3
"""Refresh the APK-bundled Mesa Turnip driver to the latest upstream build.

Newer Turnip is where the Adreno performance extensions land, so xenia-thor
tracks upstream instead of pinning a driver (standing directive, CLAUDE.md).
This fetches the newest AdrenoTools package, drops it into the APK assets, and
rewrites BUNDLED_TURNIP_VERSION so the in-app auto-installer upgrades devices
that are still on the previous bundled driver.

    python tools/update_turnip.py            # check + update if newer
    python tools/update_turnip.py --check    # report only, change nothing
    python tools/update_turnip.py --tag TAG  # pin a specific release

Source: The412Banner/Banners-Turnip, which rebuilds from Mesa main on every
upstream commit. The plain (non-suffixed) asset is the A6xx/A7xx build - the
right one for the AYN Thor's Adreno 740. These are automated builds and are not
guaranteed stable, so DEVICE-VALIDATE after updating: launch a title from the
in-app GUI and confirm the log reports driverID VK_DRIVER_ID_MESA_TURNIP, not 8
(VK_DRIVER_ID_QUALCOMM_PROPRIETARY).

Rebuild + reinstall the APK afterwards; the driver ships inside it.
"""

import argparse
import io
import json
import re
import sys
import urllib.request
import zipfile
from pathlib import Path

REPO = "The412Banner/Banners-Turnip"
REPO_ROOT = Path(__file__).resolve().parent.parent
ASSET_PATH = (
    REPO_ROOT
    / "android/android_studio_project/app/src/main/assets/drivers/turnip.zip"
)
MANAGER_PATH = (
    REPO_ROOT
    / "android/android_studio_project/app/src/main/java/jp/xenia/emulator"
    / "GpuDriverManager.java"
)
VERSION_RE = re.compile(
    r'(public static final String BUNDLED_TURNIP_VERSION = ")([^"]*)(";)'
)


def fetch_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "xenia-thor"})
    with urllib.request.urlopen(req, timeout=60) as response:
        return json.load(response)


def current_version():
    match = VERSION_RE.search(MANAGER_PATH.read_text(encoding="utf-8"))
    return match.group(2) if match else "<unknown>"


def pick_asset(release):
    """The A6xx/A7xx build: the asset with no variant suffix (A8xx, -Test...)."""
    candidates = [
        asset
        for asset in release.get("assets", [])
        if asset["name"].endswith(".zip")
        and re.fullmatch(r"Turnip-v[^/]*?\.zip", asset["name"])
        and "A8xx" not in asset["name"]
        and "Test" not in asset["name"]
    ]
    if not candidates:
        raise SystemExit(
            "No A6xx/A7xx asset in release {}; assets were: {}".format(
                release.get("tag_name"),
                [a["name"] for a in release.get("assets", [])],
            )
        )
    # Shortest name = the plain build, when a release carries several variants.
    return min(candidates, key=lambda a: len(a["name"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="report versions only, write nothing"
    )
    parser.add_argument("--tag", help="use this release tag instead of the latest")
    args = parser.parse_args()

    if args.tag:
        release = fetch_json(
            "https://api.github.com/repos/{}/releases/tags/{}".format(REPO, args.tag)
        )
    else:
        release = fetch_json(
            "https://api.github.com/repos/{}/releases/latest".format(REPO)
        )

    tag = release["tag_name"]
    new_version = tag.lstrip("v")
    have = current_version()
    print("bundled: {}\nlatest:  {}  ({})".format(have, new_version, release["published_at"]))

    if have == new_version:
        print("Already current.")
        return 0
    if args.check:
        print("Update available (run without --check to apply).")
        return 0

    asset = pick_asset(release)
    print("downloading {} ({} bytes)".format(asset["name"], asset["size"]))
    req = urllib.request.Request(
        asset["browser_download_url"], headers={"User-Agent": "xenia-thor"}
    )
    with urllib.request.urlopen(req, timeout=300) as response:
        payload = response.read()

    # Validate before overwriting: an AdrenoTools package is a meta.json plus the
    # .so it names. Shipping a bad zip would silently fall back to the Qualcomm
    # driver on every device.
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        names = archive.namelist()
        if "meta.json" not in names:
            raise SystemExit("Rejected: no meta.json in {}".format(asset["name"]))
        meta = json.loads(archive.read("meta.json"))
        library = meta.get("libraryName", "")
        if library not in names:
            raise SystemExit(
                "Rejected: meta.json names '{}' which is not in the zip".format(library)
            )
    print(
        "validated: {} | {} | {}".format(
            meta.get("name"), meta.get("driverVersion"), library
        )
    )

    ASSET_PATH.write_bytes(payload)
    source = MANAGER_PATH.read_text(encoding="utf-8")
    MANAGER_PATH.write_text(
        VERSION_RE.sub(lambda m: m.group(1) + new_version + m.group(3), source),
        encoding="utf-8",
        newline="",
    )
    print(
        "\nUpdated {} -> {}\n"
        "  {}\n  {}\n\n"
        "Next: rebuild + reinstall the APK, then launch a title from the in-app\n"
        "GUI and confirm the log reports driverID VK_DRIVER_ID_MESA_TURNIP.".format(
            have, new_version, ASSET_PATH, MANAGER_PATH
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
