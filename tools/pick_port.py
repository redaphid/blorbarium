"""Choose the upload port by USB identity, not by whichever name it landed on.

From claude-notification-screen. Two boards on one desk get whichever COM
number Windows hands out, and the failure is flashing 240x240 firmware onto
the 412x412 board, which comes up looking broken rather than looking wrong.
The VID:PID can't be confused, so each env declares the one it wants as
custom_usb_id.

No match aborts the upload and lists what is actually attached. Falling back to
PlatformIO's auto-detect would put us straight back to picking a board at
random, which is the thing this exists to stop.
"""
from SCons.Script import COMMAND_LINE_TARGETS

Import("env")

# Only when a target actually needs a port. A plain `pio run` is a compile and
# must work with nothing plugged in at all.
needs_port = {"upload", "monitor", "uploadfs", "erase"} & set(map(str, COMMAND_LINE_TARGETS))

want = env.GetProjectOption("custom_usb_id", None)
if needs_port and want and not env.subst("$UPLOAD_PORT"):
    from serial.tools import list_ports

    attached = list(list_ports.comports())
    matches = [p.device for p in attached if want.lower() in (p.hwid or "").lower()]
    if len(matches) == 1:
        env.Replace(UPLOAD_PORT=matches[0], MONITOR_PORT=matches[0])
        print("[pick-port] %s -> %s" % (want, matches[0]))
    else:
        for p in attached:
            print("[pick-port]   %s  %s" % (p.device, p.hwid))
        raise SystemExit(
            "[pick-port] %d ports match %s. Attach the right board, or pass --upload-port."
            % (len(matches), want)
        )
