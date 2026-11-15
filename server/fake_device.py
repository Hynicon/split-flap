"""A stand-in for the display, for trying the app and the server without hardware.

It talks to the server exactly like the ESP32 does (POST /api/device/sync) and keeps a rough copy
of the display's behaviour: it applies settings, picks random words and builds a frame. The real
rules live in the firmware (firmware/master/src/logic); this only has to be close enough to look at.

    python server/fake_device.py http://127.0.0.1:8000 <DEVICE_TOKEN>
"""

from __future__ import annotations

import json
import random
import sys
import time
import unicodedata
import urllib.error
import urllib.request

LOGOS = {"tiktok": "", "instagram": "", "facebook": ""}
PLATFORMS = ["tiktok", "instagram", "facebook"]
MONTHS = {"en": "JAN FEB MAR APR MAY JUN JUL AUG SEP OCT NOV DEC".split(),
          "pt": "JAN FEV MAR ABR MAI JUN JUL AGO SET OUT NOV DEZ".split()}
FLAPS = set(" ABCDEFGHIJKLMNOPQRSTUVWXYZÃÇÉÁ0123456789.")

config = {
    "platforms": {
        "tiktok": {"enabled": True, "source": "manual", "manual_count": 18400},
        "instagram": {"enabled": True, "source": "manual", "manual_count": 5231},
        "facebook": {"enabled": True, "source": "manual", "manual_count": 912},
    },
    "poll_minutes": 15, "cycle_seconds": 60, "units": 9, "label_width": 2, "label_logos": True, "speed": 10,
    "mode": "cycle", "single": "instagram", "text": "", "text_align": "center",
    "quiet_start": "00:00", "quiet_end": "00:00", "timezone": "UTC0", "celebrate": False, "milestone_step": 1000,
    "random_seconds": 60, "mix": [], "clock_24h": True, "date_lang": "en",
    "words": ["TACOS", "BURRITO", "NACHOS"], "presets": ["OPEN", "SOLD OUT", "BACK SOON"],
    "weather_lat": "", "weather_lon": "", "weather_unit": "c",
    "countdown_label": "", "countdown_at": "", "countdown_done": "",
    "remote_url": "", "remote_token_set": True, "admin_key_set": True,
}
FAKE_WEATHER = {"temp": 18.4, "code": 3}  # pretend reading, in °C
show = {"mode": None, "started": 0.0, "index": -1, "word": "", "platform": -1, "left": 0}
started = time.time()


def fold(text: str) -> str:
    out = []
    for ch in text.upper():
        if ch not in FLAPS:
            ch = unicodedata.normalize("NFD", ch)[0]
        out.append(ch if ch in FLAPS else " ")
    return "".join(out)


def place(text: str, align: str = "center") -> str:
    units = config["units"]
    text = fold(text).strip()[:units]
    pad = units - len(text)
    if align == "left":
        return text + " " * pad
    if align == "right":
        return " " * pad + text
    return " " * (pad // 2) + text + " " * (pad - pad // 2)


def followers(platform: str) -> str:
    units, width = config["units"], config["label_width"]
    label = (LOGOS[platform] + " " * (width - 1)) if config["label_logos"] else {"tiktok": "TT", "instagram": "IG", "facebook": "FB"}[platform][:width]
    return (label + str(config["platforms"][platform]["manual_count"]).rjust(units - width))[:units]


def clock() -> str:
    now = time.localtime()
    if config["clock_24h"]:
        return place(time.strftime("%H %M", now))
    return place(f"{now.tm_hour % 12 or 12} {now.tm_min:02d} {'AM' if now.tm_hour < 12 else 'PM'}")


def date() -> str:
    now = time.localtime()
    return place(f"{now.tm_mday} {MONTHS[config['date_lang']][now.tm_mon - 1]}")


def weather() -> str:
    if config["weather_lat"] == "":
        return " " * config["units"]
    t = FAKE_WEATHER["temp"] * 9 / 5 + 32 if config["weather_unit"] == "f" else FAKE_WEATHER["temp"]
    n = f"{round(abs(t))}{config['weather_unit'].upper()}"
    word = {"en": "CLOUD", "pt": "NUVEM"}[config["date_lang"]]
    forms = [f"MINUS {n}", f"M{n}"] if round(t) < 0 else [f"{word} {n}", n]
    return next((place(f) for f in forms if len(f) <= config["units"]), " " * config["units"])


def countdown() -> str:
    at = config["countdown_at"]
    if not at:
        return " " * config["units"]
    now = time.time()
    if "T" in at:
        left = time.mktime(time.strptime(at, "%Y-%m-%dT%H:%M")) - now
    else:
        h, m = map(int, at.split(":"))
        t = time.localtime(now)
        target = time.mktime((t.tm_year, t.tm_mon, t.tm_mday, h, m, 0, 0, 0, -1))
        left = target - now if target > now else target + 86400 - now
    if left <= 0:
        return place(config["countdown_done"] or config["countdown_label"])
    left = int(left)
    if left >= 2 * 86400:
        d, h = divmod(left, 86400)
        full, brief = f"{d}D" + (f"{h // 3600}H" if h // 3600 else ""), f"{d}D"
    elif left >= 3600:
        full, brief = f"{left // 3600}H{left % 3600 // 60:02d}M", f"{left // 3600}H"
    else:
        full = brief = f"{(left + 59) // 60}M"
    label = fold(config["countdown_label"]).strip()
    forms = ([f"{label} {full}", f"{label} {brief}"] if label else []) + [full, brief]
    return next((place(f) for f in forms if len(f) <= config["units"]), " " * config["units"])


def enabled() -> list[str]:
    return [p for p in PLATFORMS if config["platforms"][p]["enabled"]]


def next_platform() -> str | None:
    on = enabled()
    if not on:
        return None
    for step in range(1, 4):
        candidate = PLATFORMS[(show["platform"] + step) % 3]
        if candidate in on:
            show["platform"] = PLATFORMS.index(candidate)
            return candidate
    return None


def next_word() -> bool:
    words = [w for w in config["words"] if w != show["word"]] or config["words"]
    if not words:
        show["word"] = ""
        return False
    show["word"] = random.choice(words)
    return True


def frame() -> str:
    """Roughly what logic/rotation.cpp and App::desiredFrame do."""
    mode, now = config["mode"], time.time()
    if show["mode"] != mode:
        show.update(mode=mode, started=now, index=-1, left=0)
        fresh = True
    else:
        fresh = False
    elapsed = now - show["started"]
    blank = " " * config["units"]
    if mode == "off":
        return blank
    if mode == "text":
        return place(config["text"], config["text_align"])
    if mode == "clock":
        return clock()
    if mode == "date":
        return date()
    if mode == "weather":
        return weather()
    if mode == "countdown":
        return countdown()
    if mode == "single":
        return followers(config["single"])
    if mode == "cycle":
        if fresh or elapsed >= config["cycle_seconds"] or PLATFORMS[show["platform"]] not in enabled():
            show["started"] = now
            if next_platform() is None:
                return blank
        return followers(PLATFORMS[show["platform"]])
    if mode == "random":
        if fresh or elapsed >= config["random_seconds"] or show["word"] not in config["words"]:
            show["started"] = now
            next_word()
        return place(show["word"], config["text_align"]) if show["word"] else blank
    mix = config["mix"]
    if not mix:
        return blank
    item = mix[show["index"]] if 0 <= show["index"] < len(mix) else None
    if item is None or elapsed >= item["seconds"]:
        if item and item["kind"] == "followers" and show["left"] > 0 and next_platform():
            show["left"] -= 1
            show["started"] = now
        else:
            for _ in range(len(mix)):
                show["index"] = (show["index"] + 1) % len(mix)
                item = mix[show["index"]]
                usable = {"followers": bool(enabled()), "random": bool(config["words"])}.get(item["kind"], True)
                if usable:
                    break
            else:
                return blank
            show["started"] = now
            if item["kind"] == "followers":
                show["left"] = len(enabled()) - 1
                next_platform()
            elif item["kind"] == "random":
                next_word()
    kind = mix[show["index"]]["kind"]
    return {"followers": lambda: followers(PLATFORMS[show["platform"]]), "random": lambda: place(show["word"], config["text_align"]),
            "text": lambda: place(config["text"], config["text_align"]), "clock": clock, "date": date,
            "weather": weather, "countdown": countdown}[kind]()


def status() -> dict:
    platforms = {}
    for p in PLATFORMS:
        c = config["platforms"][p]
        platforms[p] = {"enabled": c["enabled"], "source": c["source"], "count": c["manual_count"], "last_ok": int(time.time()),
                        "status": "ok", "error": "manual", "failures": 0, "next_fetch_in_s": 0,
                        "token": {"access": "", "refresh": "", "expires_at": 0, "refresh_expires_at": 0, "needs_reauth": False}}
    return {"uptime_s": int(time.time() - started), "reset_reason": "power_on", "free_heap": 180000, "time": int(time.time()),
            "wifi": {"ssid": "fake-hotspot", "rssi": -58, "ip": "10.0.0.2"},
            "display": {"frame": frame(), "quiet": False, "busy": False},
            "bus": {"units": config["units"], "present": [True] * config["units"], "error": ""}, "platforms": platforms,
            "weather": {"wanted": config["mode"] == "weather" or any(m["kind"] == "weather" for m in config["mix"]),
                        "temp": (round(FAKE_WEATHER["temp"] * 9 / 5 + 32, 1) if config["weather_unit"] == "f" else FAKE_WEATHER["temp"]) if config["weather_lat"] != "" else None, "code": FAKE_WEATHER["code"],
                        "unit": config["weather_unit"], "last_ok": int(time.time()), "error": ""}}


def run(cmd: dict) -> str:
    kind, body = cmd["type"], cmd.get("body") or {}
    if kind == "config":
        if body.get("random_seconds", 20) < 20 or body.get("cycle_seconds", 20) < 20:
            return "must be an integer 20..3600"
        if len(body.get("words", [])) > 100:
            return "words can hold at most 100 entries"
        for key, value in body.items():
            if key == "platforms":
                for p, fields in value.items():
                    config["platforms"][p].update(fields)
            elif key in config:
                config[key] = value
    elif kind == "show":
        config.update(mode="text", text=body.get("text", ""))
        if body.get("align"):
            config["text_align"] = body["align"]
    elif kind == "rehome":
        print("re-homing")
    elif kind == "tokens":
        print("tokens received for", body.get("platform"))
    return ""


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    url, token = sys.argv[1].rstrip("/") + "/api/device/sync", sys.argv[2]
    results, send_config, last_id = [], True, 0
    while True:
        body = {"results": results, "status": status()}
        if send_config:
            body["config"] = config
        req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json", "Authorization": f"Bearer {token}"})
        try:
            with urllib.request.urlopen(req, timeout=40) as res:
                reply = json.load(res)
        except (urllib.error.URLError, TimeoutError, ValueError) as e:
            print("sync failed:", e)
            time.sleep(5)
            continue
        results, send_config = [], False
        for cmd in reply.get("commands", []):
            result = {"id": cmd["id"]}
            if cmd["id"] > last_id:
                last_id = cmd["id"]
                error = run(cmd)
                print(cmd["type"], json.dumps(cmd.get("body"), ensure_ascii=False)[:100], "->", error or "ok")
                if error:
                    result["error"] = error
                send_config = True
            results.append(result)
        # With nothing to report, pause briefly so the frame in the next status is not stale for long.
        if not results:
            time.sleep(0.2)


if __name__ == "__main__":
    main()
