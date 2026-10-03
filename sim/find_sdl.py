"""Find SDL2 for the simulator on whatever this is, rather than assuming homebrew.

In order: sdl2-config, pkg-config, then a prefix -- SDL2_PREFIX, or homebrew's
on a Mac. A prefix with headers but no libSDL2 to link (a -dev package unpacked
without root) links the runtime library the system already has.
"""
import ctypes.util
import glob
import os
import shutil
import subprocess
import sys

Import("env")


def ask(*cmd):
    if not shutil.which(cmd[0]):
        return None
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 and r.stdout.strip() else None


flags = ask("sdl2-config", "--cflags", "--libs") or ask("pkg-config", "--cflags", "--libs", "sdl2")
if flags:
    env.MergeFlags(flags)
else:
    prefix = os.environ.get("SDL2_PREFIX") or os.environ.get("HOMEBREW_PREFIX") or (
        ask("brew", "--prefix") if sys.platform == "darwin" else None)
    header = prefix and next((d for d in (os.path.join(prefix, "include"), os.path.join(prefix, "usr", "include"))
                              if os.path.exists(os.path.join(d, "SDL2", "SDL.h"))), None)
    if not header:
        raise SystemExit("[find-sdl] no SDL2 headers. apt install libsdl2-dev, brew install sdl2, "
                         "or SDL2_PREFIX=/where/it/is")
    env.Append(CPPPATH=[header, os.path.join(header, "SDL2")])
    # Debian keeps the per-architecture half of SDL_config.h one directory down.
    env.Append(CPPPATH=glob.glob(os.path.join(header, "*-linux-gnu*")))
    libdir = os.path.join(prefix, "lib")
    if any(f.startswith("libSDL2.") for f in (os.listdir(libdir) if os.path.isdir(libdir) else [])):
        env.Append(LIBPATH=[libdir], LIBS=["SDL2"])
    else:
        # Linux names the runtime libSDL2-2.0.so.0, which a search for "SDL2" walks past.
        runtime = ctypes.util.find_library("SDL2") or ctypes.util.find_library("SDL2-2.0")
        if not runtime:
            raise SystemExit("[find-sdl] SDL2 headers at %s but no SDL2 library anywhere" % header)
        env.Append(LIBS=[":" + runtime])   # LIBS, not LINKFLAGS: it has to follow the objects that need it
    print("[find-sdl] %s" % header)
