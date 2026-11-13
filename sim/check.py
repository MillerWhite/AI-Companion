#!/usr/bin/env python3
"""Smoke check of the headless simulator: Pip follows the finger and the tilt, stays inside the round screen,
and every reaction fires from its real input path (touch, accelerometer, buttons).
Usage: python3 sim/check.py [path/to/pipsim]"""
import os, re, subprocess, sys, tempfile
from PIL import Image

sim = sys.argv[1] if len(sys.argv) > 1 else "build/pipsim"
tmp = tempfile.mkdtemp()

def run(events, secs=1.5, name="x"):
    path = os.path.join(tmp, name + ".png")
    out = subprocess.run([sim, "--out", path, "--secs", str(secs), "--events", events],
                         check=True, capture_output=True, text=True).stdout
    status = dict(re.findall(r"(\w+)=(\S*)", out.splitlines()[-1]))
    # brightness = the strongest channel, so tinted eyes (red rage, green envy) still count as eyes
    rgb = Image.open(path).convert("RGB")
    r, g, b = rgb.split()
    from PIL import ImageChops
    return ImageChops.lighter(ImageChops.lighter(r, g), b), status

def pupils(img):
    """centroid of the dark-grey pupil pixels (#1C1C1C), which is where Pip is looking"""
    g = lambda x, y: 15 < img.getpixel((x, y)) < 45
    # solid patches only: the antialiased rims of the white eyes pass through the same greys for a pixel or two
    inside = lambda x, y: (x - 233) ** 2 + (y - 233) ** 2 < 225 ** 2  # the bezel around the glass is grey too
    pts = [(x, y) for y in range(3, 463, 2) for x in range(3, 463, 2) if inside(x, y) and all(g(x + dx, y + dy) for dx, dy in
           ((0, 0), (3, 0), (-3, 0), (0, 3), (0, -3)))]
    return sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)

def eyes(img):
    """centroid x/y of bright pixels and the farthest one from the screen center"""
    pts = [(x, y) for y in range(0, 466, 2) for x in range(0, 466, 2) if img.getpixel((x, y)) > 128]
    if not pts: return 0, 0, 0
    far = max(((x + 0.5 - 233) ** 2 + (y + 0.5 - 233) ** 2) ** 0.5 for x, y in pts)
    return sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts), far

def check(name, cond, info):
    print(("ok  " if cond else "FAIL") + "  " + name + ("" if cond else f"  {info}"))
    return cond

ok = True
r = subprocess.run([os.path.join(os.path.dirname(sim), "face_test")], capture_output=True, text=True)
ok &= check("renderer: no clipped or shaved eyes (face_test)", r.returncode == 0, r.stderr.strip())
imgs = {n: run(e, name=n)[0] for n, e in [("left", "0:k5 0.1:d20,233"), ("right", "0:k5 0.1:d446,233"), ("up", "0:k5 0.1:d233,20")]}
left, right, up = (eyes(imgs[n]) for n in ("left", "right", "up"))
pl, pr, pu = (pupils(imgs[n]) for n in ("left", "right", "up"))
ok &= check("looks at the finger", pl[0] < 213 < 253 < pr[0] and pu[1] < 213, (pl, pr, pu))
ok &= check("stays inside the screen", all(e[2] < 231 for e in (left, right, up)), (left, right, up))

# tilt the right side down: accelerometer x goes negative, eyes slide right
still, _ = run("0:a0,1,0", 1.5, "still")
tilted, _ = run("0:a0,1,0 0.5:a-0.5,0.87,0", 1.5, "tilted")
ok &= check("tilt moves the gaze downhill", pupils(tilted)[0] > pupils(still)[0] + 15, (pupils(still), pupils(tilted)))

_, st = run("0.3:S 1.3:S", 1.6)
ok &= check("shake -> dizzy", st["mood"] == "dizzy", st)
_, st = run("0.2:a0,0,-1", 2.0)
ok &= check("face down -> asleep", st["mood"] == "asleep" and st["face_down"] == "1", st)
_, st = run("0.2:a0,0,-1 1.6:a0,1,0", 2.0)
ok &= check("turned back up -> surprised", st["mood"] == "surprised", st)
img, st = run("0.2:a0,0,-1", 3.0)
ok &= check("face down a second asleep -> the screen goes dark", st["display"] == "0" and eyes(img)[2] == 0, st)
img, st = run("0.2:a0,0,-1 2.6:a0,1,0", 3.0)
ok &= check("turned over -> the screen is back", st["display"] == "1" and eyes(img)[2] > 0, st)
_, st = run("0.2:a0,0,-1 2.6:P", 3.0)
ok &= check("a button wakes the dark screen and does nothing else", st["display"] == "1" and st["screen"] == "pip", st)
_, st = run("0.2:a0,0,-1 2.6:P", 8.5)
ok &= check("still face down: dark again 5 s later", st["display"] == "0", st)
_, st = run("0.2:W", 5.2)
ok &= check("walking counts steps (1.8/s for 5s)", 7 <= int(st["steps"]) <= 10, st)
_, st = run("0.1:V35 0.5:Q", 1.5)
ok &= check("blowing on the mic -> he screws his eyes up", st["reaction"] == "blown", st)
_, st = run("0.1:V85", 1.5)
ok &= check("a loud room is not breath", st["reaction"] != "blown", st)
_, st = run("0.1:J7", 1.0)
ok &= check("battery under 10% -> tired", st["mood"] == "tired", st)
_, st = run("0.1:J50 0.4:J50+", 1.0)
ok &= check("plugged in -> happy (dinner)", st["reaction"] == "charging", st)
_, st = run("0.2:W0.6", 5.2)
ok &= check("a hard walk, keys swinging, still counts (and is not a shake)", 7 <= int(st["steps"]) <= 10 and st["mood"] != "dizzy", st)
_, st = run("0.2:d233,233 0.25:u 0.5:d233,233 0.55:u 0.8:d233,233 0.85:u", 1.0)
ok &= check("three pokes -> fed up (angry)", st["reaction"] == "fed" and st["mood"] == "angry", st)
_, st = run("0.2:d233,233 0.25:u", 0.4)
ok &= check("one poke -> ouch", st["mood"] == "ouch", st)
stroke = " ".join(f"{0.2 + i * 0.05:.2f}:m{150 if i % 8 < 4 else 320},233" for i in range(70))
_, st = run("0.15:d150,233 " + stroke, 1.5)
ok &= check("stroke -> pet (happy)", st["mood"] == "happy", st)
_, st = run("0.15:d150,233 " + stroke, 3.8)
ok &= check("long stroke -> love", st["mood"] == "love", st)
_, st = run("0.2:p1", 1.0)
ok &= check("bottom button (BOOT) -> meet (curious)", st["mood"] == "curious", st)
_, st = run("0.2:p1", 2.0)
ok &= check("meet ends in like or dislike", st["reaction"] in ("likes", "dislikes", "shy", "wary"), st)
# the pet behind the face
r = subprocess.run([os.path.join(os.path.dirname(sim), "pet_test")], capture_output=True, text=True)
ok &= check("pet logic over simulated days (pet_test)", r.returncode == 0, r.stderr.strip())
_, st = run("0.2:T8 0.5:F", 1.0)
ok &= check("feeding a hungry pet -> yum", st["reaction"] == "yum", st)
_, st = run("0.3:F", 0.8)
ok &= check("feeding a full pet -> too full", st["reaction"] == "too" and st["mood"] == "nauseous", st)
_, st = run("0.2:T13", 1.0)
ok &= check("01:00 -> asleep on its own", st["mood"] == "asleep" and st["asleep"] == "1", st)
_, st = run("0.2:T13 0.6:d233,233 0.65:u", 1.0)
ok &= check("poke at night wakes him up", st["reaction"] == "woken" and st["asleep"] == "0", st)
_, st = run("0.2:T21", 1.0)
ok &= check("a day of neglect shows in the mood", st["mood"] in ("angry", "furious", "annoyed", "belligerent", "lonely", "despondent", "hurt", "sad",
                                                                  "bored", "cynical", "gloomy", "nervous", "sick"), st)
_, st = run("0.2:nrage 0.4:A", 1.0)
ok &= check("A returns to the automatic mood", st["auto"] == "1" and st["mood"] != "rage", st)

# the mood tree: every branch from its real input. `why` is kind.which (ui/pet.h): 1 rare card, 2 body, 3 aftertaste,
# 4 need, 5 night, 6 situation, 7 palette. K<knob>=<v> sets a knob of the tree, as the debug page does.
def pokes(n, t=0.2):
    return " ".join(f"{t + i * 0.25:.2f}:d233,233 {t + i * 0.25 + 0.05:.2f}:u" for i in range(n))
_, st = run(pokes(6), 1.9)
ok &= check("poked on: the second fed-up is furious", st["reaction"] == "fed" and st["mood"] == "furious", st)
_, st = run(pokes(12), 3.4)
ok &= check("poked on and on: swearing", st["reaction"] == "fed" and st["mood"] == "swearing", st)
_, st = run(pokes(3), 5.0)
ok &= check("fed up wears off as a minute of annoyed (aftertaste)", st["mood"] == "annoyed" and st["why"] == "3.7", st)
_, st = run("0.05:Kmeet_like=200 0.06:Kmeet_shy=1 0.2:p1", 2.0)
ok &= check("meet: liked, shy", st["reaction"] == "shy" and st["mood"] == "shy", st)
_, st = run("0.05:Kmeet_like=200 0.06:Kmeet_shy=0 0.2:p1", 2.0)
ok &= check("meet: liked -> love", st["reaction"] == "likes" and st["mood"] == "love", st)
_, st = run("0.05:Kmeet_like=-100 0.06:Kmeet_wary=1 0.2:p1", 2.0)
ok &= check("meet: disliked, wary -> suspicious", st["reaction"] == "wary" and st["mood"] == "suspicious", st)
_, st = run("0.05:Kmeet_like=-100 0.06:Kmeet_wary=0 0.2:p1", 2.0)
ok &= check("meet: disliked -> angry", st["reaction"] == "dislikes" and st["mood"] == "angry", st)
_, st = run("0.05:Kmeet_like=-100 " + pokes(3) + " 1.2:p1", 3.0)
ok &= check("meet while annoyed: disliked -> belligerent", st["mood"] == "belligerent", st)
_, st = run("0.1:V35 2.5:V95 2.9:V35", 3.2)
ok &= check("a bang (95 dB over a quiet room) -> shocked", st["reaction"] == "bang" and st["mood"] == "shocked", st)
_, st = run("0.1:V40 2.5:V80 2.9:V40", 3.2)
ok &= check("a smaller bang (80 dB) -> afraid", st["reaction"] == "bang" and st["mood"] == "afraid", st)
_, st = run("0.1:V35 2.5:V95 2.9:V35", 6.0)
ok &= check("after a bang he stays afraid a while (aftertaste)", st["mood"] == "afraid" and st["why"] == "3.3", st)
_, st = run("0.1:V35 2.5:Q 4:Q", 6.0)
ok &= check("blowing on the mic is no bang", st["base"] != "afraid", st)
_, st = run("0.2:a0,-1,0", 2.0)
ok &= check("held upside down -> puzzled", st["reaction"] == "upside" and st["mood"] == "puzzled", st)
_, st = run("0.2:a0,-1,0 2.0:a0,1,0", 2.6)
ok &= check("turned back over: puzzled no more", st["reaction"] != "upside" and st["mood"] != "puzzled", st)
_, st = run("0.3:a0,0,0 0.6:a0,1,0", 1.2)
ok &= check("dropped (free fall) -> shocked, then afraid", st["reaction"] == "dropped" and st["mood"] == "shocked" and st["base"] == "afraid", st)
_, st = run("0.2:a0,0,-1 1.6:a0,1,0 2.0:a0,0,-1 3.4:a0,1,0", 4.0)
ok &= check("put to bed and picked up twice in a row -> indignant", st["reaction"] == "woke" and st["mood"] == "indignant", st)
_, st = run("0.2:a0,0,-1 1.6:a0,1,0", 4.0)
ok &= check("just woken: sleepy for a while (situation)", st["mood"] == "sleepy" and st["why"] == "6.0", st)
_, st = run("0.1:H3 0.2:L2 0.4:N 0.5:N 0.6:N", 1.2)
ok &= check("three notifications at once -> dismayed", st["reaction"] == "flood" and st["mood"] == "dismayed" and st["why"] == "3.4", st)
_, st = run("0.1:H3 0.2:L2 0.4:N 6:N 12:N", 12.5)
ok &= check("notifications one by one: no flood", st["reaction"] != "flood" and st["base"] != "dismayed", st)
_, st = run("0.2:L2 0.4:C1 0.7:P", 1.2)
ok &= check("call answered -> he nods (yes)", st["reaction"] == "answered" and st["mood"] == "yes", st)
_, st = run("0.2:L2 0.4:C1 0.7:B", 1.2)
ok &= check("call declined -> he shakes his head (no)", st["reaction"] == "declined" and st["mood"] == "no", st)
_, st = run("0.2:L2 0.4:C1 0.8:C0", 1.4)
ok &= check("missed call -> puzzled", st["reaction"] == "missed" and st["mood"] == "puzzled", st)
_, st = run("0.2:L1 0.5:P", 1.0)
ok &= check("phone paired -> yes", st["reaction"] == "paired" and st["mood"] == "yes", st)
_, st = run("0.2:T6 0.6:d233,233 0.65:u", 1.2)
ok &= check("back after 6 h, loved enough -> glad (love, then happy for a while)",
            st["reaction"] == "glad" and st["mood"] == "love" and st["base"] == "happy", st)
_, st = run("0.2:T10 0.6:d233,233 0.65:u", 1.2)
ok &= check("back after 10 h, neglected -> sulks (hurt)", st["reaction"] == "sulks" and st["mood"] == "hurt" and st["why"] == "3.5", st)
stroke_late = " ".join(f"{1.05 + i * 0.05:.2f}:m{150 if i % 8 < 4 else 320},233" for i in range(70))
_, st = run("0.2:T10 0.6:d233,233 0.65:u 1.0:d150,233 " + stroke_late, 5.0)
ok &= check("sulking, stroked -> made up", st["why"] != "3.5", st)
for card in ("stoned", "pinkeye"):
    _, st = run(f"0.2:R{card}", 1.0)
    ok &= check(f"rare card -> {card}", st["mood"] == card and st["why"].startswith("1."), st)
_, st = run("0.1:J15", 1.0)
ok &= check("battery 15% -> nervous", st["mood"] == "nervous" and st["why"] == "2.3", st)
_, st = run("0.1:J3", 1.0)
ok &= check("battery 3% -> sick", st["mood"] == "sick" and st["why"] == "2.0", st)
_, st = run("0.1:J50 0.4:J50+ 0.6:T0.05", 6.0)
ok &= check("on the charger a while -> comfy", st["mood"] == "comfy" and st["why"] == "6.2", st)
_, st = run("0.2:W", 20.0)
ok &= check("out on a walk -> curious / amazed", st["mood"] in ("curious", "amazed") and st["why"] == "6.1", st)
_, st = run("0.3:F 0.6:F", 1.0)
ok &= check("fed twice when full -> disgusted", st["reaction"] == "enough" and st["mood"] == "disgusted", st)
_, st = run("0.2:T8 0.5:G", 1.0)
ok &= check("bored, offered a game -> yes", st["reaction"] == "play" and st["mood"] == "yes", st)
_, st = run("0.2:T8.5", 1.0)
ok &= check("20:30 -> the evening palettes", st["why"].startswith("7.") and int(st["why"][2:]) >= 4, st)
def xo(turns):  # endless noughts and crosses with Pip: each turn taps cells in this order, the first free one counts
    ev, t = ["0.1:oxo"], 0.6
    for pri in turns:
        for c in map(int, pri):
            ev += [f"{t:.2f}:d{137 + c % 3 * 96},{137 + c // 3 * 96}", f"{t + 0.02:.2f}:u"]
            t += 0.04
        t += 0.8
    return run(" ".join(ev), round(t, 2))[1]
games = {}
for turns in (["715428360", "527461308", "380176542", "082634751"],
              ["385761204", "508623174", "053426187", "843025716", "836427105", "128704356"]):
    st = xo(turns)
    games[st["reaction"]] = st
    ok &= check(f"noughts and crosses: {st['reaction']} -> " + ("smug" if st["reaction"] == "won" else "no, then indignant"),
                (st["reaction"], st["mood"], st["base"]) in (("won", "smug", "smug"), ("lost", "no", "indignant")), st)
ok &= check("noughts and crosses: he both won and lost (if not, Pip's moves changed: pick new taps)", set(games) == {"won", "lost"}, games)
# yes / no: the eyes swing up and down / side to side
for name, axis in (("yes", 1), ("no", 0)):
    pos = [eyes(run(f"0:n{name}", 1.5 + i * 0.06, "sw")[0])[:2] for i in range(8)]
    swing = [max(p[k] for p in pos) - min(p[k] for p in pos) for k in (0, 1)]
    ok &= check(f"{name}: the eyes swing {'up and down' if axis else 'side to side'}", swing[axis] > 12 and swing[axis] > 2 * swing[1 - axis], swing)

# apps: navigation with a finger and the side buttons
flick0 = "0.5:d360,240 0.6:m300,240 0.7:m200,240 0.75:u"
flick = "0.01:H0 " + flick0  # Тишина (hidden in the Style app) keeps the honeycomb tests
_, st = run(flick, 1.2)
ok &= check("quick flick on Pip -> menu (not petting)", st["screen"] == "menu" and st["reaction"] != "pet", st)
_, st = run(flick + " 1.2:d233,233 1.25:u", 1.8)
ok &= check("honeycomb: tap the middle icon -> clock", st["screen"] == "clock", st)
_, st = run(flick + " 1.2:d171,125 1.25:u", 1.8)
ok &= check("honeycomb: tap a neighbour -> care", st["screen"] == "care", st)
_, st = run(flick + " 1.2:d330,330 1.3:m260,260 1.4:m180,180 1.45:u 2.2:P", 2.8)
ok &= check("honeycomb: pan, top button opens the app now in focus (not clock)", st["screen"] not in ("clock", "menu", "pip"), st)
_, st = run(flick + " 1.2:d40,233 1.25:u 1.35:d40,233 1.4:u", 1.9)
ok &= check("honeycomb: double tap on nothing -> Pip", st["screen"] == "pip", st)
_, st = run("0.01:H0 0.2:Y " + flick0 + " 1.2:d330,196 1.3:m250,196 1.4:m160,196 1.45:u 2:d233,196 2.05:u", 2.6)
ok &= check("carousel style still works: flick, tap -> care", st["screen"] == "care", st)
_, st = run(flick + " 1.2:d233,233 1.25:u 2:d233,150 2.05:m233,220 2.1:m233,300 2.15:u", 2.8)
ok &= check("swipe down in an app -> menu", st["screen"] == "menu", st)
_, st = run("0.2:P 0.7:P", 1.2)
ok &= check("top button: Pip -> menu -> the app in the middle", st["screen"] == "clock", st)
_, st = run("0.2:oqr 0.6:B 1.2:B", 1.8)
ok &= check("bottom button: app -> menu -> Pip", st["screen"] == "pip", st)
_, st = run("0.3:B", 1.0)
ok &= check("bottom button on Pip -> meet", st["reaction"] == "meet", st)
_, st = run("0.2:ostopwatch 0.5:P 1.0:P 1.4:B", 1.8)
ok &= check("stopwatch keeps both buttons (start/lap, reset)", st["screen"] == "stopwatch", st)
_, st = run("0.2:otimer 0.6:d179,325 0.65:u", 3.0)
ok &= check("timer starts", st["screen"] == "timer", st)
_, st = run("0.01:H0 0.2:otimer 0.4:d62,320 0.5:m85,350 0.6:m100,372 0.7:u 0.9:d179,325 0.95:u", 64)  # knob to 1 min, start
ok &= check("1-minute timer rings: back to Pip, surprised", st["screen"] == "pip" and st["reaction"] == "time's", st)
_, st = run("0.3:S 1.0:S 1.8:S 2.5:S 3.3:S 4.0:S", 4.5)
ok &= check("shaken three times in a row -> dull for a while", st["mood"] == "dull" and st["reaction"] == "shaken", st)

# themes: picked in the Style app, the list menu, and every app opening in every theme
_, st = run("0.05:H3 0.2:ostyle 0.5:d233,194 0.55:u 0.9:P", 1.4)
ok &= check("Style app (Диагностика): tap Приборка, top button applies -> theme 1", st["theme"] == "1" and st["screen"] == "menu", st)
_, st = run("0.05:H1 0.2:ostyle 0.6:d233,200 0.65:u", 1.4)
ok &= check("Style app (Приборка): tap Диагностика -> theme 3; Тишина and Apple are not offered", st["theme"] == "3" and st["screen"] == "menu", st)
_, st = run("0.2:ostyle 0.6:d233,200 0.65:u 1.2:ostyle 1.5:d233,194 1.55:u 1.9:P 2.5:P", 3.0)
ok &= check("style back and forth from inside the Style app, then the menu opens the app in focus again", st["theme"] == "1" and st["screen"] == "style", st)
_, st = run("0.3:P", 0.8)
ok &= check("Приборка is the default style", st["theme"] == "1", st)
_, st = run("0.1:H1 " + flick0 + " 1.2:d233,233 1.25:u", 1.8)
ok &= check("Приборка list menu: tap the middle row -> clock", st["screen"] == "clock", st)
_, st = run("0.1:H1 " + flick0 + " 1.2:d233,300 1.25:m233,260 1.3:m233,244 1.6:u 2.2:d233,233 2.25:u", 2.8)
ok &= check("list menu: drag up a row, hold, let go, tap the middle -> care", st["screen"] == "care", st)
_, st = run("0.1:H3 0.2:P 0.5:d233,330 0.53:m233,280 0.56:m233,230 0.59:m233,180 0.6:u 2.2:P", 2.6)
ok &= check("list menu: a flick coasts several rows and lands on one", st["screen"] not in ("menu", "clock", "care", "qr"), st)
app_ids = ["bt", "clock", "care", "qr", "noise", "badge", "games", "xo", "eight", "coin", "dice", "maze", "timer", "stopwatch", "torch", "ball", "style", "notify", "music", "camera", "remote"]
# the maze: the physics on its own (perfect mazes, a steered ball gets out, nothing shakes it through a wall), then the app
r = subprocess.run([os.path.join(os.path.dirname(sim), "maze_test")], capture_output=True, text=True)
ok &= check("maze: perfect mazes, a steered ball gets out, never through a wall (maze_test)", r.returncode == 0, r.stderr.strip())
def blob(img):  # the ball: the only white thick enough for a 9x9 square (walls are 4-5 px)
    w = lambda x, y: img.getpixel((x, y)) > 200
    pts = [(x, y) for y in range(70, 400, 2) for x in range(70, 400, 2) if all(w(x + dx, y + dy) for dx in (-4, 0, 4) for dy in (-4, 0, 4))]
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)) if pts else None
held = "0:a0,0.6,0.8 0.1:H1 0.3:omaze"
b0, b1 = blob(run(held, 0.9)[0]), blob(run(held + " 1.0:a0,0.9,0.43 2.0:a-0.4,0.82,0.4", 3.4)[0])
ok &= check("maze: tilt down, then right -> the ball rolls down and right", b0 and b1 and b1[1] > b0[1] + 30 and b1[0] > b0[0] + 30, (b0, b1))
_, st = run(held + " 1.0:P", 1.4)
ok &= check("maze: top button deals a new one, stays in the app", st["screen"] == "maze", st)
_, st = run("0.1:H1 0.3:ogames 0.8:B", 1.6)
ok &= check("games in the list menu: bottom button goes back to the menu, not to Pip", st["screen"] == "menu", st)
_, st = run("0.1:H1 0.3:odice 0.6:B", 1.4)
ok &= check("back out of a game lands in the list menu (on the games)", st["screen"] == "menu", st)
_, st = run("0.1:L2 0.4:N", 0.9)
ok &= check("a notification shows a banner (its text hidden by default)", st["screen"] == "banner", st)
_, st = run("0.1:L2 0.3:onotify 0.6:d233,86 0.65:u 0.9:B 1.3:N", 1.7)
ok &= check("privacy off: no banner, nothing kept", st["screen"] != "banner", st)
_, st = run("0.05:H1", 33)
ok &= check("left alone 30 s: the screen sleeps", st["display"] == "0", st)
_, st = run("0.05:H1 34:d233,233 34.1:u", 36)
ok &= check("asleep: a touch wakes him", st["display"] == "1", st)
_, st = run("0.05:H1 0:a0,0,1 34:a0,0.6,0.8", 36)
ok &= check("asleep: picked up (turned) wakes him", st["display"] == "1", st)
_, st = run("0.05:H1 0.3:obadge", 36)
ok &= check("the badge holds him awake", (st["display"], st["screen"]) == ("1", "badge"), st)
img, st = run("0.3:Z", 40); img = img.convert("RGB")
ok &= check("updating: the notice stays lit until the reset", st["display"] == "1" and img.getpixel((233, 140)) == (0, 0, 0) and max(img.crop((120, 170, 346, 210)).getdata()) > (200, 200, 200), st)
for t in range(4):
    bad = []
    for a in app_ids:
        img, st = run(f"0.1:H{t} 0.3:o{a}", 0.9, "th")
        lit = sum(1 for y in range(0, 466, 6) for x in range(0, 466, 6) if abs(img.getpixel((x, y)) - img.getpixel((233, 8))) > 40)
        want = "menu" if a == "games" and t in (1, 3) else a  # in the list menu, "games" turns the list into the games
        if st["screen"] != want or lit < 20: bad.append((a, st["screen"], lit))
    ok &= check(f"theme {t}: all {len(app_ids)} apps open and draw something", not bad, bad)

# Диагностика: the terminal layouts drive the same apps through the buttons the hints promise
_, st = run("0.1:H3 0.2:P 0.5:d233,300 0.55:m233,260 0.6:m233,244 0.9:u 1.5:P", 1.9)
ok &= check("Диагностика list: drag up a row, top button opens the focused app -> care", st["screen"] == "care", st)
_, st = run("0.1:H3 0.2:ostopwatch 0.5:B", 0.9)
ok &= check("stopwatch at zero: bottom button leaves -> menu", st["screen"] == "menu", st)
_, st = run("0.1:H3 0.2:otimer 0.4:d243,40 0.45:m240,40 0.5:u 0.8:P", 63.5)
ok &= check("Диагностика timer: ring set to 1 min, top button starts, it rings", st["screen"] == "pip" and st["reaction"] == "time's", st)

# the iPhone (a fake one): pairing, the banner, calls, music and the camera send the right commands back
_, st = run("0.1:H3 0.2:L1", 0.6)
ok &= check("phone: pairing shows the code on top", st["screen"] == "pair", st)
_, st = run("0.1:H3 0.2:L1 0.5:P", 0.9)
ok &= check("phone: top button confirms the code -> pair_ok, back to Pip", st["phone"] == "pair_ok" and st["screen"] == "pip", st)
_, st = run("0.1:H3 0.2:L2 0.4:N", 0.8)
ok &= check("phone: a notification drops a banner over Pip", st["screen"] == "banner", st)
_, st = run("0.1:H3 0.2:L2 0.4:N", 7.0)
ok &= check("phone: the banner goes by itself after 6 s", st["screen"] == "pip", st)
_, st = run("0.1:H3 0.2:L2 0.4:N 0.8:P 1.3:P", 1.7)
ok &= check("phone: banner -> top opens it -> top again marks it read on the phone", st["phone"] == "note_dismiss" and st["screen"] == "notify", st)
_, st = run("0.1:H3 0.2:L2 0.4:N 0.5:N 0.6:N 0.8:onotify 1.2:B", 1.6)
ok &= check("phone: the notification list leaves with the bottom button", st["screen"] == "menu", st)
_, st = run("0.1:H3 0.2:L2 0.4:C1", 0.8)
ok &= check("phone: an incoming call takes the screen", st["screen"] == "call", st)
_, st = run("0.1:H3 0.2:L2 0.4:C1 0.7:B", 1.0)
ok &= check("phone: bottom button declines the call", st["phone"] == "call_decline" and st["screen"] == "pip", st)
_, st = run("0.1:H3 0.2:L2 0.3:M1 0.4:omusic 0.8:P", 1.1)
ok &= check("phone: music, top button -> play_pause", st["phone"] == "play_pause" and st["screen"] == "music", st)
_, st = run("0.1:H3 0.2:L2 0.3:M1 0.4:omusic 0.8:d330,240 0.83:m230,240 0.86:m120,240 0.9:u", 1.2)
ok &= check("phone: music, swipe left -> next track", st["phone"] == "next", st)
_, st = run("0.1:H3 0.2:L2 0.3:M1 0.4:omusic 0.8:d233,200 0.9:m233,240 1.0:m233,280 1.1:m233,320 1.2:u", 1.5)
ok &= check("phone: music, drag down turns the volume down and stays in the app", st["phone"] == "vol_down" and st["screen"] == "music", st)
_, st = run("0.1:H3 0.2:L2 0.4:ocamera 0.8:P", 4.2)
ok &= check("phone: camera, top button, 3 s countdown -> one shot", st["shots"] == "1" and st["screen"] == "camera", st)
_, st = run("0.1:H3 0.2:L2 0.4:ocamera 0.8:P 1.5:B", 4.2)
ok &= check("phone: camera, bottom button cancels the countdown", st["shots"] == "0", st)
# the slide remote: a computer pairs like the phone does, the buttons turn slides, a held finger aims the pointer
_, st = run("0.1:H1 0.3:oremote", 0.8)
ok &= check("remote without a computer: shows how to pair, the computer gets nothing", st["screen"] == "remote" and st["host"] == "off", st)
_, st = run("0.1:H1 0.3:oremote 0.6:P", 1.0)
ok &= check("remote without a computer: a button goes back to the menu, nothing is sent", st["screen"] == "menu" and st["clicks"] == "0", st)
_, st = run("0.1:H3 0.2:X1", 0.6)
ok &= check("remote: a computer asks to pair -> the code on top", st["screen"] == "pair" and st["host"] == "pairing", st)
_, st = run("0.1:H3 0.2:X1 0.5:P", 0.9)
ok &= check("remote: top button confirms the code -> the computer is on", st["host"] == "on" and st["screen"] == "pip", st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.6:P 0.9:P 1.2:B", 1.6)
ok &= check("remote: top button twice -> Page Down twice, bottom -> Page Up, no clicks, stays in the app",
            (st["nexts"], st["prevs"], st["clicks"], st["screen"]) == ("2", "1", "0", "remote"), st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.5:B1.2 0.6:g20,0,0 1.6:g0,0,0", 2.0)
ok &= check("remote: bottom held, Pip raised -> the page scrolls down, no Page Up", int(st["scroll"]) >= 5 and st["prevs"] == "0", st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.5:B1.2 0.6:g-20,0,0 1.6:g0,0,0", 2.0)
ok &= check("remote: bottom held, Pip lowered -> the page scrolls up", int(st["scroll"]) <= -5 and st["prevs"] == "0", st)
_, st = run("0:a0,0,1 0.1:H1 0.1:X2 0.3:oremote 0.5:B1.2 0.6:g20,0,0 1.6:g0,0,0", 2.0)
ok &= check("remote: flat in the palm, bottom held, top edge raised -> scrolls down too", int(st["scroll"]) >= 5, st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.5:B1.2", 2.0)
ok &= check("remote: bottom held still -> nothing at all (no Page Up, no scroll)", (st["prevs"], st["scroll"]) == ("0", "0"), st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.5:B0.15 0.55:g20,0,0 0.9:g0,0,0", 1.2)
ok &= check("remote: a quick press while the hand moves is still Page Up, not a scroll", (st["prevs"], st["scroll"]) == ("1", "0"), st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.5:d233,233 0.6:g0,-30,0 1.6:g0,0,0 1.9:u", 2.2)
ok &= check("remote: finger held, turn right -> the pointer goes right, no click", int(st["mx"]) > 300 and abs(int(st["my"])) < 20 and st["clicks"] == "0", st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.5:d233,233 0.6:g20,0,0 1.6:g0,0,0 1.9:u", 2.2)
ok &= check("remote: finger held, tip up -> the pointer goes up", int(st["my"]) < -200 and abs(int(st["mx"])) < 20, st)
_, st = run("0:a0,0,1 0.1:H1 0.1:X2 0.3:oremote 0.5:d233,233 0.6:g0,0,-30 1.6:g0,0,0 1.9:u", 2.2)
ok &= check("remote: flat in the palm, turned right about the vertical (z) -> the pointer goes right too", int(st["mx"]) > 300 and abs(int(st["my"])) < 20, st)
_, st = run("0:a0,0,1 0.1:H1 0.1:X2 0.3:oremote 0.5:d233,233 0.6:g20,0,0 1.6:g0,0,0 1.9:u", 2.2)
ok &= check("remote: flat in the palm, top edge tipped up -> the pointer goes up", int(st["my"]) < -200 and abs(int(st["mx"])) < 20, st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.4:g5,-4,3 2.4:d233,233 2.5:g5,-4,3 3.5:u", 3.8)
ok &= check("remote: the gyroscope's steady drift is learnt while no finger is down", abs(int(st["mx"])) < 40 and abs(int(st["my"])) < 40, st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.6:g0,-30,0 1.6:g0,0,0", 1.9)
ok &= check("remote: no finger, no pointer (the hand can wave freely)", st["mx"] == "0" and st["my"] == "0", st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.6:d233,233 0.7:u", 1.0)
ok &= check("remote: a short tap clicks", st["clicks"] == "1", st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.6:d233,233 0.65:u 0.75:d233,233 0.8:u", 1.2)
ok &= check("remote: a double tap is a double click, no drag", (st["clicks"], st["drags"]) == ("2", "0"), st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.6:d233,233 0.65:u 0.75:d233,233 1.1:g0,-30,0 1.8:g0,0,0", 2.0)
ok &= check("remote: tap, then hold and turn -> a drag: the button held while the pointer moves", (st["clicks"], st["drags"], st["held"]) == ("0", "1", "1") and int(st["mx"]) > 150, st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.6:d233,233 0.65:u 0.75:d233,233 1.1:g0,-30,0 1.8:g0,0,0 1.9:u", 2.2)
ok &= check("remote: the finger lifted ends the drag (the button let go)", (st["drags"], st["held"]) == ("1", "0"), st)
_, st = run("0.1:H1 0.1:X2 0.3:oremote 0.6:d233,150 0.65:m233,250 0.7:m233,350 0.75:u", 1.4)
ok &= check("remote: swipe down leaves the app", st["screen"] != "remote", st)
# the music app on a computer: the same gestures as media keys; with the phone here too a tap picks which one
_, st = run("0.1:H1 0.1:X2 0.3:omusic 0.6:P", 1.0)
ok &= check("music on a computer: top button -> play/pause media key", st["media"] == "play_pause" and st["screen"] == "music", st)
_, st = run("0.1:H1 0.1:X2 0.3:omusic 0.6:d330,240 0.63:m230,240 0.66:m120,240 0.7:u", 1.2)
ok &= check("music on a computer: swipe left -> next track key", st["media"] == "track_next", st)
_, st = run("0.1:H1 0.1:X2 0.3:omusic 0.6:d233,200 0.7:m233,240 0.8:m233,280 0.9:m233,320 1.0:u", 1.5)
ok &= check("music on a computer: drag down -> volume down key, stays in the app", st["media"] == "vol_down" and st["screen"] == "music", st)
_, st = run("0.1:H1 0.1:X2 0.15:L2 0.2:M1 0.3:omusic 0.6:P", 1.0)
ok &= check("music with the phone and a computer: the computer by default", st["media"] == "play_pause" and st["phone"] == "-", st)
_, st = run("0.1:H1 0.1:X2 0.15:L2 0.2:M1 0.3:omusic 0.6:d233,233 0.65:u 1.0:P", 1.4)
ok &= check("music with both: a tap -> the iPhone gets the next press", st["phone"] == "play_pause" and st["media"] == "-", st)
_, st = run("0.1:H1 0.1:X2 0.15:L2 0.2:M1 0.3:omusic 0.6:d233,233 0.65:u 1.0:d233,233 1.05:u 1.5:P", 1.9)
ok &= check("music with both: a second tap -> back to the computer", st["media"] == "play_pause" and st["phone"] == "-", st)
_, st = run("0.1:H1 0.1:X2 0.15:L2 0.2:M1 0.3:omusic 0.6:X0 0.9:P", 1.3)
ok &= check("music: the computer leaves -> the iPhone's music again", st["phone"] == "play_pause" and st["screen"] == "music", st)
for t in range(4):
    img, st = run(f"0.1:H{t} 0.1:X2 0.3:omusic", 0.8, "th")
    lit = sum(1 for y in range(0, 466, 6) for x in range(0, 466, 6) if abs(img.getpixel((x, y)) - img.getpixel((233, 8))) > 40)
    ok &= check(f"theme {t}: music on a computer draws", st["screen"] == "music" and lit > 20, (st["screen"], lit))
for t in range(4):
    img, st = run(f"0.1:H{t} 0.1:X2 0.3:oremote", 0.8, "th")
    lit = sum(1 for y in range(0, 466, 6) for x in range(0, 466, 6) if abs(img.getpixel((x, y)) - img.getpixel((233, 8))) > 40)
    ok &= check(f"theme {t}: the remote draws its pad with a computer on", st["screen"] == "remote" and lit > 20, (st["screen"], lit))
for t in (0, 2):
    bad = []
    for a in ("notify", "music", "camera"):
        img, st = run(f"0.1:H{t} 0.15:L2 0.25:M1 0.3:o{a} 0.5:N", 0.9, "th")  # in the list app a new one lands in the list
        lit = sum(1 for y in range(0, 466, 6) for x in range(0, 466, 6) if abs(img.getpixel((x, y)) - img.getpixel((233, 8))) > 40)
        if st["screen"] != (a if a == "notify" else "banner") or lit < 20: bad.append((a, st["screen"], lit))
    ok &= check(f"theme {t}: the phone apps draw with a phone connected (banners over the others)", not bad, bad)

# voice: the synth itself, and Pip speaking through the real paths
r = subprocess.run([os.path.join(os.path.dirname(sim), "voice_test")], capture_output=True, text=True)
ok &= check("voice: 16 sounds audible, ending, pitched right (voice_test)", r.returncode == 0, r.stderr.strip())
import wave, struct
def loudness(events, secs, windows):
    path = os.path.join(tmp, "v.wav")
    subprocess.run([sim, "--out", os.path.join(tmp, "v.png"), "--wav", path, "--secs", str(secs), "--events", events], check=True, capture_output=True)
    w = wave.open(path); d = struct.unpack(f"<{w.getnframes()}h", w.readframes(w.getnframes()))
    return [(sum(x * x for x in d[int(a * 16000):int(b * 16000)]) / max(1, int((b - a) * 16000))) ** .5 for a, b in windows]
quiet, poked = loudness("0.5:d233,233 0.55:u", 1.0, [(0, .45), (.5, .8)])
ok &= check("silent at rest, a poke says eep", quiet < 50 and poked > 1000, (quiet, poked))
rings = loudness("0.01:H0 0.2:otimer 0.4:d62,320 0.5:m85,350 0.6:m100,372 0.7:u 0.9:d179,325 0.95:u", 63, [(61.0, 62.5)])[0]
ok &= check("the timer sings when it rings", rings > 1000, rings)
ticks = loudness("0.1:H3 0.2:P 0.6:d233,330 0.63:m233,280 0.66:m233,230 0.69:m233,180 0.7:u", 2.0, [(0.3, 0.55), (0.6, 1.4)])
ok &= check("the menu wheel clicks as rows pass, silent before", ticks[0] < 50 and ticks[1] > 300, ticks)
curse = loudness("0:nswearing", 11, [(0, 0.5), (1.2, 4.8), (5.0, 5.5), (6.2, 9.8), (10.0, 10.5)])
ok &= check("swearing: one curse and its beep every 5 s, quiet between", min(curse[0::2]) > 1000 and max(curse[1::2]) < 50, curse)
muted = loudness("0.01:H0 0.2:ocare 0.5:d378,366 0.55:u 1:B 1.4:B 2:d233,233 2.05:u", 2.6, [(2.05, 2.4)])[0]
ok &= check("sound off in the care app mutes him", muted < 50, muted)

# Quick QR: every code on screen must scan back to its link (a phone camera's job, done by OpenCV)
try:
    import cv2
    links = re.findall(r'"(https?://[^"]+)"', open(os.path.join(os.path.dirname(__file__), "..", "ui", "profile.c")).read())  # the default links
    for theme in (0, 3):  # dark modules on white, and the terminal's background-coloured modules on its ink plate
        ev = f"0.1:H{theme} 0.2:oqr"
        for k in range(len(links)):
            path = os.path.join(tmp, f"qr{k}.png")
            subprocess.run([sim, "--out", path, "--secs", str(0.6 + k * 0.6), "--events", ev], check=True, capture_output=True)
            got = cv2.QRCodeDetector().detectAndDecode(cv2.imread(path))[0]
            ok &= check(f"theme {theme}: QR {k + 1} scans to {links[k]}", got == links[k], got)
            t = 0.6 + k * 0.6  # swipe left to the next code
            ev += f" {t:.2f}:d330,240 {t + .03:.2f}:m230,240 {t + .06:.2f}:m120,240 {t + .09:.2f}:u"
except ImportError:
    print("skip  QR scan check (no OpenCV)")

names = subprocess.run([sim, "--list"], check=True, capture_output=True, text=True).stdout.split()
ok &= check(f"{len(names)} moods, all with a table entry", len(names) >= 40, names)
for n in names:  # every mood shows up as itself and draws eyes (asleep is a thin line, still bright)
    img, st = run(f"0:n{n}", 2.0, "m")
    ok &= check(f"mood {n} renders", st["mood"] == n and eyes(img)[2] > 0, st)
print("ok" if ok else "FAILED")
sys.exit(0 if ok else 1)
