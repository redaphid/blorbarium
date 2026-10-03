"""BLORB_FW: the commit this firmware was built from, so the serial log says what is running."""
import subprocess

Import("env")  # noqa: F821

try:
    stamp = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
                           capture_output=True, text=True, check=True).stdout.strip() or "unstamped"
except (OSError, subprocess.CalledProcessError):
    stamp = "unstamped"
env.Append(CPPDEFINES=[("BLORB_FW", env.StringifyMacro(stamp))])  # noqa: F821
