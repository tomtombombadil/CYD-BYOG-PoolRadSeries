# PlatformIO pre-build script: the firmware version comes from the VERSION
# file at the repo root (semantic versioning: X.Y.Z - a fix bumps Z, a new
# feature bumps Y, a major revision bumps X). Defines
#   BYOG_VERSION  "v0.1.0"
#   BYOG_BUILD    the git commit ("1a2b3c4"), or "" outside a git checkout
# Runs inside PlatformIO's own Python, on Windows and in CI alike.
# (Same script as CYD-Classic-Games, renamed defines.)
import os
import subprocess

Import("env")  # noqa: F821 - provided by PlatformIO

root = env.subst("$PROJECT_DIR")  # noqa: F821
with open(os.path.join(root, "VERSION"), encoding="utf-8") as f:
    version = "v" + f.read().strip()
# CI passes the release tag; a pre-release suffix (v1.2.0-beta.1) shows too
tag = os.environ.get("CYD_VERSION", "")
if tag == version or tag.startswith(version + "-"):
    version = tag
try:
    build = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=root,
                                    stderr=subprocess.DEVNULL, text=True).strip()
except Exception:
    build = ""
env.Append(CPPDEFINES=[("BYOG_VERSION", env.StringifyMacro(version)),  # noqa: F821
                       ("BYOG_BUILD", env.StringifyMacro(build))])
print("CYD BYOG Pool of Radiance Series", version, build)
