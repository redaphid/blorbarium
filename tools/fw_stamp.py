"""BLORB_FW: the commit this firmware was built from, so the serial log says what is running.

Only src/build_info.cpp is built with it: as a define on every file, each new
commit recompiled the whole firmware and missed the compile cache.
"""
import os
import subprocess

Import("env")  # noqa: F821

try:
    stamp = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
                           capture_output=True, text=True, check=True).stdout.strip() or "unstamped"
except (OSError, subprocess.CalledProcessError):
    stamp = "unstamped"
build_info = os.path.join(env.subst("$PROJECT_SRC_DIR"), "build_info.cpp")  # noqa: F821


def stamped(env, node):
    if os.path.normcase(node.srcnode().get_abspath()) != os.path.normcase(build_info):
        return node
    env = env.Clone()
    env.Append(CPPDEFINES=[("BLORB_FW", env.StringifyMacro(stamp))])
    return env.Object(node)


env.AddBuildMiddleware(stamped)  # noqa: F821
