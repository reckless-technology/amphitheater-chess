#!/usr/bin/env python3
"""Generate a classic Staunton-style chess piece set as 12 transparent SVGs."""
import os

OUT = "pieces"
os.makedirs(OUT, exist_ok=True)

THEMES = {
    "w": dict(g0="#fffdf6", g1="#e3d6b8", stroke="#1d1a16", detail="#1d1a16"),
    "b": dict(g0="#4a4440", g1="#161412", stroke="#0b0a09", detail="#c9bba0"),
}

# Shared plinth: two tiers.
BASE = [
    "M18,92 L82,92 Q85,92 85,89 L85,86 Q85,82.5 80,82 L20,82 Q15,82.5 15,86 L15,89 Q15,92 18,92 Z",
    "M23,82 L77,82 Q77,77 71,76 L29,76 Q23,77 23,82 Z",
]

def collar(y, hw):
    return f"M{50-hw},{y} Q50,{y-5} {50+hw},{y} Q50,{y+5} {50-hw},{y} Z"

PIECES = {
    "P": dict(
        shapes=[
            "M43,46 Q44,62 32,76 L68,76 Q56,62 57,46 Z",
            collar(45, 12),
            "circle:50,29,11.5",
        ],
        details=[],
    ),
    "R": dict(
        shapes=[
            "M32,76 L36,42 L64,42 L68,76 Z",
            "M29,43 L71,43 L71,38 L29,38 Z",
            "M30,38 L30,20 L38,20 L38,26 L45.5,26 L45.5,20 L54.5,20 L54.5,26 L62,26 L62,20 L70,20 L70,38 Z",
        ],
        details=["M34,70 L66,70", "M31,32 L69,32"],
    ),
    "N": dict(
        shapes=[
            "M31,76 C31,63 39,56 46,50 C40,52.5 34,54.5 28,54.5 "
            "C21.5,54.5 17.5,50.5 18.5,46 C19.5,41.5 26,33.5 33.5,26 "
            "C37.5,22 41.5,18.5 45.5,16 L47.5,7.5 L53.5,16.5 "
            "C65,23 73,44 69,76 Z",
        ],
        details=[
            "M53.5,21 C62,28 66.5,45 64.5,70",   # mane line
            "dot:36.5,30.5,2.2",                  # eye
            "dot:23,47.5,1.5",                    # nostril
            "M31,70 L68.5,70",
        ],
    ),
    "B": dict(
        shapes=[
            "M38,76 Q43,62 44.5,52 L55.5,52 Q57,62 62,76 Z",
            collar(51, 11),
            "M50,15 C39.5,23 35.5,34 40,43.5 Q50,48 60,43.5 C64.5,34 60.5,23 50,15 Z",
            "circle:50,11.5,4.2",
        ],
        details=["M55.5,25 L46.5,36"],
    ),
    "Q": dict(
        shapes=[
            "M34,76 Q42.5,59 43,46 L57,46 Q57.5,59 66,76 Z",
            collar(45, 15),
            "M36.5,42 L31,21 L41,31 L43.5,14.5 L50,29 L56.5,14.5 L59,31 L69,21 L63.5,42 Z",
            "circle:31,19.5,3.4", "circle:43.5,13,3.4", "circle:56.5,13,3.4",
            "circle:69,19.5,3.4", "circle:50,26.5,2.6",
        ],
        details=["M38,37 Q50,34 62,37"],
    ),
    "K": dict(
        shapes=[
            "M34,76 Q42.5,59 43,46 L57,46 Q57.5,59 66,76 Z",
            collar(45, 15),
            "M37,42.5 Q28.5,31 36.5,24.5 Q44,19.5 50,28.5 Q56,19.5 63.5,24.5 Q71.5,31 63,42.5 Z",
            "M47,5 L53,5 L53,10 L58,10 L58,16 L53,16 L53,27 L47,27 L47,16 L42,16 L42,10 L47,10 Z",
        ],
        details=["M38.5,37.5 Q50,34.5 61.5,37.5"],
    ),
}

def elem(s, attrs):
    if s.startswith("circle:"):
        cx, cy, r = s[7:].split(",")
        return f'<circle cx="{cx}" cy="{cy}" r="{r}" {attrs}/>'
    return f'<path d="{s}" {attrs}/>'

def svg(color, kind):
    t = THEMES[color]
    p = PIECES[kind]
    gid = f"g{color}{kind}"
    body_attr = (f'fill="url(#{gid})" stroke="{t["stroke"]}" stroke-width="2.4" '
                 f'stroke-linejoin="round" stroke-linecap="round"')
    parts = [elem(s, body_attr) for s in BASE + p["shapes"]]
    # A thin highlight line on the plinth adds a turned-wood feel.
    parts.append(f'<path d="M20,87 L80,87" fill="none" stroke="{t["detail"]}" '
                 f'stroke-width="1.2" stroke-opacity="0.55" stroke-linecap="round"/>')
    for d in p["details"]:
        if d.startswith("dot:"):
            cx, cy, r = d[4:].split(",")
            parts.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{t["detail"]}"/>')
        else:
            parts.append(f'<path d="{d}" fill="none" stroke="{t["detail"]}" '
                         f'stroke-width="1.8" stroke-linecap="round"/>')
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">\n'
        f'  <defs><linearGradient id="{gid}" x1="0.2" y1="0" x2="0.8" y2="1">'
        f'<stop offset="0" stop-color="{t["g0"]}"/><stop offset="1" stop-color="{t["g1"]}"/>'
        f'</linearGradient></defs>\n  ' + "\n  ".join(parts) + "\n</svg>\n"
    )

for c in "wb":
    for k in PIECES:
        with open(f"{OUT}/{c}{k}.svg", "w") as f:
            f.write(svg(c, k))
print("ok")
