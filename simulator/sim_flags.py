# Adds SDL2 and C++17 flags for the simulator build.
#
# macOS workarounds for mismatched developer tools (seen with an Xcode 27 beta on macOS 26):
# - if the xcrun shims of Xcode are broken, use the standalone Command Line Tools instead
# - if the default SDK is newer than the running macOS, its libraries may list architectures
#   the linker does not know yet; then use the SDK that matches the macOS version

Import("env")

import glob
import os
import platform
import re
import shutil
import subprocess
import sys

if sys.platform == "darwin":
    tool_env = dict(os.environ)

    if "DEVELOPER_DIR" not in os.environ:
        ok = subprocess.run(["xcrun", "--find", "clang++"], capture_output=True).returncode == 0
        clt = "/Library/Developer/CommandLineTools"
        if not ok and os.path.isdir(clt):
            print("sim_flags.py: xcrun is broken, using " + clt)
            env["ENV"]["DEVELOPER_DIR"] = clt
            tool_env["DEVELOPER_DIR"] = clt

    if "SDKROOT" not in os.environ:
        sdk = subprocess.run(["xcrun", "--show-sdk-path"], env=tool_env, capture_output=True, text=True).stdout.strip()
        found = re.search(r"MacOSX(\d+)", sdk)
        os_major = int(platform.mac_ver()[0].split(".")[0])
        if found and int(found.group(1)) > os_major:
            matching = sorted(glob.glob(os.path.join(os.path.dirname(sdk), "MacOSX%d.*sdk" % os_major)))
            if matching:
                print("sim_flags.py: SDK %s is newer than macOS %d, using %s" % (os.path.basename(sdk), os_major, matching[-1]))
                env["ENV"]["SDKROOT"] = matching[-1]

if shutil.which("sdl2-config") is None:
    sys.exit("sdl2-config not found - install SDL2 (macOS: brew install sdl2)")

cflags = subprocess.check_output(["sdl2-config", "--cflags"]).decode().split()
libs = subprocess.check_output(["sdl2-config", "--libs"]).decode().split()

env.Append(
    CCFLAGS=cflags,
    CXXFLAGS=["-std=c++17"],
    LINKFLAGS=[f for f in libs if not f.startswith("-l")],
    LIBS=[f[2:] for f in libs if f.startswith("-l")],
)
