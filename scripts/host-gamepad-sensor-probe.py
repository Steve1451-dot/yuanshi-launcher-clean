"""Read real local SDL2 controller capabilities and bounded sensor statistics.

No synthetic input, virtual controller, cloud login, device serials or raw packet
logging. The DLL is supplied by the user-installed emulator, not downloaded.
"""
import argparse
import ctypes as c
import json
import math
import os
from pathlib import Path
import time


class Version(c.Structure):
    _fields_ = [("major", c.c_ubyte), ("minor", c.c_ubyte), ("patch", c.c_ubyte)]


class Guid(c.Structure):
    _fields_ = [("data", c.c_ubyte * 16)]


def read_controllers(dll_path, duration):
    dll_path = Path(dll_path).resolve(strict=True)
    with os.add_dll_directory(str(dll_path.parent)):
        sdl = c.CDLL(str(dll_path))
    def bind(name, result, *args):
        fn = getattr(sdl, name)
        fn.restype, fn.argtypes = result, list(args)
        return fn
    init = bind("SDL_Init", c.c_int, c.c_uint32)
    quit_sdl = bind("SDL_Quit", None)
    hint = bind("SDL_SetHint", c.c_int, c.c_char_p, c.c_char_p)
    error = bind("SDL_GetError", c.c_char_p)
    get_version = bind("SDL_GetVersion", None, c.POINTER(Version))
    count = bind("SDL_NumJoysticks", c.c_int)
    name = bind("SDL_JoystickNameForIndex", c.c_char_p, c.c_int)
    vendor = bind("SDL_JoystickGetDeviceVendor", c.c_uint16, c.c_int)
    product = bind("SDL_JoystickGetDeviceProduct", c.c_uint16, c.c_int)
    guid = bind("SDL_JoystickGetDeviceGUID", Guid, c.c_int)
    is_pad = bind("SDL_IsGameController", c.c_int, c.c_int)
    open_pad = bind("SDL_GameControllerOpen", c.c_void_p, c.c_int)
    close_pad = bind("SDL_GameControllerClose", None, c.c_void_p)
    has_sensor = bind("SDL_GameControllerHasSensor", c.c_int, c.c_void_p, c.c_int)
    sensor_on = bind("SDL_GameControllerSetSensorEnabled", c.c_int, c.c_void_p, c.c_int, c.c_int)
    get_sensor = bind("SDL_GameControllerGetSensorData", c.c_int, c.c_void_p, c.c_int, c.POINTER(c.c_float), c.c_int)
    update = bind("SDL_GameControllerUpdate", None)
    pump = bind("SDL_PumpEvents", None)
    button = bind("SDL_GameControllerGetButton", c.c_ubyte, c.c_void_p, c.c_int)
    axis = bind("SDL_GameControllerGetAxis", c.c_int16, c.c_void_p, c.c_int)
    hint(b"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", b"1")
    if init(0x2000 | 0x8000):
        raise RuntimeError((error() or b"SDL init failed").decode("utf-8", "replace"))
    version = Version()
    get_version(c.byref(version))
    pads = []
    try:
        print(json.dumps({"event": "REAL_SDL_BEGIN", "version": f"{version.major}.{version.minor}.{version.patch}",
                          "joysticks": count(), "synthetic_input": False}), flush=True)
        for index in range(count()):
            info = {"event": "DEVICE", "index": index, "name": (name(index) or b"").decode("utf-8", "replace"),
                    "vendor": vendor(index), "product": product(index), "guid": bytes(guid(index).data).hex(),
                    "game_controller": bool(is_pad(index))}
            handle = open_pad(index) if info["game_controller"] else None
            info["opened"] = bool(handle)
            sensors = {}
            if handle:
                for kind, label in ((1, "accel"), (2, "gyro"), (3, "accel_left"), (4, "gyro_left"), (5, "accel_right"), (6, "gyro_right")):
                    present = bool(has_sensor(handle, kind))
                    info[label] = present
                    if present:
                        result = sensor_on(handle, kind, 1)
                        info[label + "_enable_result"] = result
                        if result == 0:
                            sensors[kind] = {"name": label, "reads": 0, "changes": 0, "nonzero": 0, "peak_norm": 0.0, "previous": None}
                pads.append({"handle": handle, "index": index, "sensors": sensors,
                             "button_changes": 0, "previous_buttons": 0, "axis_peak": 0})
            print(json.dumps(info, ensure_ascii=False), flush=True)
        started = time.monotonic()
        while pads and time.monotonic() - started < duration:
            pump()
            update()
            for pad in pads:
                handle = pad["handle"]
                bits = sum((bool(button(handle, key)) << key) for key in range(21))
                if bits != pad["previous_buttons"]:
                    pad["button_changes"] += 1
                    pad["previous_buttons"] = bits
                pad["axis_peak"] = max(pad["axis_peak"], *(abs(axis(handle, i)) for i in range(4)))
                for kind, stats in pad["sensors"].items():
                    data = (c.c_float * 3)()
                    if get_sensor(handle, kind, data, 3) != 0:
                        continue
                    values = tuple(data)
                    if not all(math.isfinite(x) for x in values):
                        continue
                    norm = math.sqrt(sum(x * x for x in values))
                    stats["reads"] += 1
                    stats["nonzero"] += norm > 0.001
                    stats["peak_norm"] = max(stats["peak_norm"], norm)
                    if stats["previous"] is not None and values != stats["previous"]:
                        stats["changes"] += 1
                    stats["previous"] = values
            time.sleep(0.01)
        for pad in pads:
            summary = {"event": "REAL_SDL_RESULT", "index": pad["index"], "button_changes": pad["button_changes"],
                       "axis_peak": pad["axis_peak"], "sensors": []}
            for stats in pad["sensors"].values():
                summary["sensors"].append({key: (round(value, 5) if isinstance(value, float) else value)
                                           for key, value in stats.items() if key != "previous"})
            print(json.dumps(summary), flush=True)
    finally:
        for pad in pads:
            for kind in pad["sensors"]:
                sensor_on(pad["handle"], kind, 0)
            close_pad(pad["handle"])
        quit_sdl()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", required=True)
    parser.add_argument("--seconds", type=float, default=8)
    args = parser.parse_args()
    if not 0 <= args.seconds <= 60:
        parser.error("seconds must be between 0 and 60")
    read_controllers(args.dll, args.seconds)
