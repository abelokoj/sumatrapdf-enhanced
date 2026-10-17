import cairosvg, os, sys
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

LIGHT = dict(bg="#F5F2EA", surf="#FFFFFF", surf2="#EDE8DD", line="#DDD6C8",
             ink="#1F1E1C", ink2="#6B675F", accent="#C96442", accentS="#F3E1D8",
             page="#FFFFFF", canvas="#E9E4D8")
DARK = dict(bg="#1F1E1D", surf="#2A2927", surf2="#353330", line="#45423D",
            ink="#F2EFE8", ink2="#A39E94", accent="#D97757", accentS="#4A3229",
            page="#2B2A28", canvas="#191817")
SERIF = "Georgia, 'Tiempos Text', serif"
SANS = "'Segoe UI Variable', 'Segoe UI', Inter, sans-serif"

def t(x, y, s, size=13, c="ink", w=400, f=SANS, anchor="start", P=None):
    return (f'<text x="{x}" y="{y}" font-family="{f}" font-size="{size}" '
            f'font-weight="{w}" fill="{P[c] if c in P else c}" '
            f'text-anchor="{anchor}">{s}</text>')

def r(x, y, w, h, fill, rx=8, stroke=None, P=None, sw=1, op=1):
    fc = P.get(fill, fill)
    st = f' stroke="{P.get(stroke, stroke)}" stroke-width="{sw}"' if stroke else ""
    return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
            f'fill="{fc}"{st} opacity="{op}"/>')

def icon_btn(x, y, P, active=False, glyph=""):
    s = r(x, y, 32, 32, "accentS" if active else "surf", 8, P=P)
    col = P["accent"] if active else P["ink2"]
    return s + (f'<g transform="translate({x+8},{y+8})" stroke="{col}" '
                f'stroke-width="1.6" fill="none" stroke-linecap="round" '
                f'stroke-linejoin="round">{glyph}</g>')

G = {
 "hand": '<path d="M4 9V4a1.5 1.5 0 013 0v4M7 8V3a1.5 1.5 0 013 0v5M10 8V4a1.5 1.5 0 013 0v6c0 3-2 5-5 5H7c-2 0-3-1-4-3l-2-3a1.5 1.5 0 012.5-1.5L4 9"/>',
 "select": '<path d="M3 2l10 5-4 1.5L7.5 13z"/>',
 "pen": '<path d="M2 14l1-4 8-8 3 3-8 8z M9 4l3 3"/>',
 "hl": '<path d="M3 13h10 M5 10l5-7 3 2-5 7H5z"/>',
 "erase": '<path d="M6 13h8 M2 10l6-7 5 4-5 6H5z"/>',
 "lasso": '<ellipse cx="8" cy="6" rx="6" ry="4" stroke-dasharray="2 2"/><path d="M5 9c-1 2 0 4 2 5"/>',
 "shape": '<rect x="2" y="2" width="7" height="7" rx="1"/><circle cx="11" cy="11" r="3.5"/>',
 "text": '<path d="M3 3h10 M8 3v10 M6 13h4"/>',
 "note": '<path d="M3 2h10v8l-3 4H3z M10 14v-4h3"/>',
 "undo": '<path d="M5 3L2 6l3 3 M2 6h7a4 4 0 010 8H6"/>',
 "redo": '<path d="M11 3l3 3-3 3 M14 6H7a4 4 0 000 8h3"/>',
 "search": '<circle cx="7" cy="7" r="4.5"/><path d="M10.5 10.5L14 14"/>',
 "side": '<rect x="2" y="2" width="12" height="12" rx="2"/><path d="M6 2v12"/>',
 "more": '<circle cx="3" cy="8" r=".8"/><circle cx="8" cy="8" r=".8"/><circle cx="13" cy="8" r=".8"/>',
 "moon": '<path d="M13 9.5A5.5 5.5 0 016.5 3 5.5 5.5 0 1013 9.5z"/>',
 "ruler": '<rect x="1" y="5" width="14" height="6" rx="1" transform="rotate(-30 8 8)"/>',
 "mag": '<rect x="2" y="4" width="12" height="8" rx="2"/><path d="M5 8h6"/>',
 "mic": '<rect x="6" y="2" width="4" height="8" rx="2"/><path d="M4 8a4 4 0 008 0 M8 12v2"/>',
}

def page_text(x, y, w, P, lines=16, start=0):
    out = [t(x, y, "3.2  Variational Inference", 20, "ink", 600, SERIF, P=P)]
    yy = y + 30
    widths = [1, .96, .99, .93, .6, 1, .97, .98, .9, .95, .7, 1, .94, .99, .88, .5]
    for i in range(lines):
        out.append(r(x, yy, w * widths[(i+start) % 16], 7, "ink2", 3, P=P, op=.28))
        yy += 18 if (i+start) % 5 != 4 else 30
    return "\n".join(out), yy

def window(P, title, body, W=1440, H=900):
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
         r(0, 0, W, H, "bg", 0, P=P)]
    s.append(r(0, 0, W, 44, "bg", 0, P=P))
    s.append(f'<circle cx="24" cy="22" r="9" fill="{P["accent"]}"/>')
    s.append(t(40, 27, "Sumatra", 13, "ink", 600, SERIF, P=P))
    tabs = ["bishop-prml-ch3.pdf", "lecture-07-notes.pdf", "thesis-draft-v4.pdf"]
    tx = 120
    for i, nm in enumerate(tabs):
        w = 200
        if i == 0:
            s.append(r(tx, 8, w, 36, "surf", 10, P=P))
        s.append(t(tx+14, 31, nm, 12.5, "ink" if i == 0 else "ink2", 500 if i == 0 else 400, P=P))
        s.append(t(tx+w-18, 31, "×", 13, "ink2", P=P))
        tx += w + 4
    s.append(t(tx+10, 31, "+", 16, "ink2", P=P))
    for i, g in enumerate(["–", "▢", "✕"]):
        s.append(t(W-110+i*40, 28, g, 13, "ink2", P=P))
    s.append(f'<line x1="0" y1="44" x2="{W}" y2="44" stroke="{P["line"]}"/>')
    s.append(body)
    s.append("</svg>")
    return "\n".join(s)

def toolbar(P, W, active="pen", y=52):
    s = []
    s.append(icon_btn(16, y, P, glyph=G["side"]))
    s.append(r(60, y, 108, 32, "surf", 8, "line", P=P))
    s.append(t(76, y+21, "12", 13, "ink", 600, P=P)); s.append(t(100, y+21, "/ 148", 13, "ink2", P=P))
    s.append(r(176, y, 84, 32, "surf", 8, "line", P=P))
    s.append(t(218, y+21, "125 %", 13, "ink", 500, anchor="middle", P=P))
    tools = ["hand", "select", "pen", "hl", "erase", "lasso", "shape", "text", "note"]
    gw = len(tools)*36 + 8
    gx = (W - gw)//2
    s.append(r(gx, y-4, gw, 40, "surf", 12, "line", P=P))
    for i, k in enumerate(tools):
        s.append(icon_btn(gx+4+i*36, y, P, active=(k == active), glyph=G[k]))
    rx = W - 16 - 32
    for k in ["more", "moon", "search"]:
        s.append(icon_btn(rx, y, P, glyph=G[k])); rx -= 36
    s.append(icon_btn(rx-8, y, P, glyph=G["redo"])); s.append(icon_btn(rx-44, y, P, glyph=G["undo"]))
    return "\n".join(s)

def pen_favorites(P, x, y):
    s = [r(x, y, 520, 52, "surf", 14, "line", P=P)]
    favs = [("#1F1E1C", 2, "Ballpoint"), ("#C96442", 3, "Fountain"), ("#2F6FDB", 2.5, "Fountain"),
            ("#3E8E5A", 6, "Brush"), ("#F2C94C", 12, "Highlight"), ("#E57AA8", 12, "Highlight")]
    for i, (c, w, nm) in enumerate(favs):
        cx = x + 34 + i*52
        if i == 1:
            s.append(f'<circle cx="{cx}" cy="{y+26}" r="19" fill="none" stroke="{P["accent"]}" stroke-width="2"/>')
        op = .55 if "High" in nm else 1
        s.append(f'<circle cx="{cx}" cy="{y+26}" r="14" fill="{c}" opacity="{op}"/>')
        s.append(f'<circle cx="{cx}" cy="{y+26}" r="{min(w,10)/2+1}" fill="#fff" opacity=".85"/>')
    sx = x + 34 + 6*52 - 4
    s.append(f'<line x1="{sx}" y1="{y+12}" x2="{sx}" y2="{y+40}" stroke="{P["line"]}"/>')
    s.append(t(sx+16, y+22, "Thickness", 10.5, "ink2", P=P))
    s.append(r(sx+16, y+30, 110, 4, "surf2", 2, P=P)); s.append(r(sx+16, y+30, 46, 4, "accent", 2, P=P))
    s.append(f'<circle cx="{sx+62}" cy="{y+32}" r="7" fill="{P["surf"]}" stroke="{P["accent"]}" stroke-width="2"/>')
    s.append(t(sx+140, y+36, "+", 18, "ink2", P=P))
    return "\n".join(s)

def mock_reader(P, name, dark=False):
    W, H = 1440, 900
    b = [toolbar(P, W)]
    b.append(f'<line x1="0" y1="96" x2="{W}" y2="96" stroke="{P["line"]}"/>')
    b.append(r(0, 97, 220, H-97-32, "bg", 0, P=P))
    b.append(t(20, 128, "Pages", 13, "ink", 600, P=P)); b.append(t(200, 128, "Outline", 12, "ink2", anchor="end", P=P))
    for i in range(4):
        yy = 148 + i*170
        b.append(r(52, yy, 116, 150, "page", 4, "accent" if i == 1 else "line", P=P, sw=2 if i == 1 else 1))
        for k in range(7):
            b.append(r(64, yy+18+k*14, 92 - (k % 3)*14, 5, "ink2", 2, P=P, op=.22))
        b.append(t(110, yy+164, str(11+i), 11, "accent" if i == 1 else "ink2", 600 if i == 1 else 400, anchor="middle", P=P))
    b.append(f'<line x1="220" y1="97" x2="220" y2="{H-32}" stroke="{P["line"]}"/>')
    b.append(r(221, 97, W-221, H-97-32, "canvas", 0, P=P))
    px, pw = 420, 720
    b.append(r(px+1, 169, pw-2, 700, "page", 4, "line", P=P))
    txt, endy = page_text(px+64, 230, pw-128, P)
    b.append(txt)
    b.append(r(px+64, 272, (pw-128)*.93, 14, "#F2C94C", 3, P=P, op=.45))
    b.append(r(px+64, 290, (pw-128)*.6, 14, "#F2C94C", 3, P=P, op=.45))
    inkc = "#E59A7A" if dark else "#C96442"
    b.append(f'<path d="M{px+500} 352 c 60 -18 140 -4 150 20 c 8 24 -80 36 -150 26 c -50 -8 -40 -40 10 -48" '
             f'fill="none" stroke="{inkc}" stroke-width="2.6" stroke-linecap="round"/>')
    b.append(f'<path d="M{px+650} 380 q 40 30 30 90" fill="none" stroke="{inkc}" stroke-width="2.4" stroke-linecap="round"/>')
    b.append(f'<path d="M{px+668} 458 l 12 14 l 10 -16" fill="none" stroke="{inkc}" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>')
    b.append(t(px+560, 500, "ELBO &#8804; log p(x) &#8212; why?", 17, inkc, 400, "'Segoe Print', 'Comic Neue', cursive", P=P))
    b.append(f'<rect x="{px+60}" y="560" width="300" height="74" rx="2" fill="none" stroke="#2F6FDB" stroke-width="2.4"/>')
    b.append(r(px+60, 640, 170, 24, "accentS", 6, P=P))
    b.append(t(px+72, 656, "Snapped to rectangle", 11, "accent", 600, P=P))
    b.append(f'<g transform="translate({px+pw-40},540)"><rect width="26" height="26" rx="6" fill="#F2C94C"/>'
             f'<path d="M7 9h12M7 14h8" stroke="#6B5410" stroke-width="1.6" stroke-linecap="round"/></g>')
    b.append(pen_favorites(P, (W-520)//2 + 60, 108))
    fx = W - 70
    b.append(r(fx, 300, 48, 168, "surf", 14, "line", P=P))
    for i, k in enumerate(["ruler", "mag", "mic", "shape"]):
        b.append(icon_btn(fx+8, 308+i*40, P, glyph=G[k]))
    b.append(r(0, H-32, W, 32, "bg", 0, P=P))
    b.append(f'<line x1="0" y1="{H-32}" x2="{W}" y2="{H-32}" stroke="{P["line"]}"/>')
    b.append(t(16, H-11, "Page 12 of 148  &#183;  4 annotations on page  &#183;  Autosaved to recovery copy", 11.5, "ink2", P=P))
    b.append(t(W-16, H-11, "Pen-only mode  &#183;  Finger scrolls", 11.5, "ink2", anchor="end", P=P))
    return window(P, name, "\n".join(b), W, H)

def mock_pen_panel(P):
    W, H = 960, 640
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}">', r(0, 0, W, H, "canvas", 0, P=P)]
    x, y = 220, 60
    s.append(r(x, y, 520, 520, "surf", 18, "line", P=P))
    s.append(t(x+28, y+44, "Pen settings", 20, "ink", 600, SERIF, P=P))
    s.append(t(x+492, y+44, "Pinned", 12, "ink2", anchor="end", P=P))
    kinds = ["Ballpoint", "Fountain", "Brush", "Pencil", "Highlighter"]
    for i, k in enumerate(kinds):
        bx = x+28 + i*94
        s.append(r(bx, y+70, 86, 92, "accentS" if i == 1 else "surf2", 12, "accent" if i == 1 else None, P=P, sw=1.5))
        s.append(f'<path d="M{bx+20} {y+120} q 22 -30 46 0" fill="none" stroke="{P["ink"]}" '
                 f'stroke-width="{[1.5,3,5,1.5,10][i]}" stroke-linecap="round" opacity="{[1,1,1,.6,.35][i]}"/>')
        s.append(t(bx+43, y+150, k, 11.5, "accent" if i == 1 else "ink2", 600 if i == 1 else 400, anchor="middle", P=P))
    s.append(t(x+28, y+196, "Preview", 12, "ink2", 600, P=P))
    s.append(r(x+28, y+206, 464, 70, "page", 10, "line", P=P))
    s.append(f'<path d="M{x+50} {y+250} c 40 -40 80 30 120 0 s 80 -30 120 0 s 80 30 120 -10" fill="none" '
             f'stroke="{P["accent"]}" stroke-width="3.5" stroke-linecap="round"/>')
    def slider(lbl, val, frac, yy):
        o = [t(x+28, yy, lbl, 12.5, "ink", 500, P=P), t(x+492, yy, val, 12.5, "ink2", anchor="end", P=P),
             r(x+28, yy+12, 464, 4, "surf2", 2, P=P), r(x+28, yy+12, 464*frac, 4, "accent", 2, P=P),
             f'<circle cx="{x+28+464*frac}" cy="{yy+14}" r="8" fill="{P["surf"]}" stroke="{P["accent"]}" stroke-width="2"/>']
        return "\n".join(o)
    s.append(slider("Thickness", "0.9 pt", .35, y+310))
    s.append(slider("Opacity", "100 %", 1, y+356))
    s.append(slider("Pressure sensitivity", "Medium", .55, y+402))
    s.append(t(x+28, y+452, "Color", 12.5, "ink", 500, P=P))
    cols = ["#1F1E1C", "#C96442", "#2F6FDB", "#3E8E5A", "#8E5AC9", "#F2C94C", "#E57AA8", "#FFFFFF"]
    for i, c in enumerate(cols):
        cx = x+40 + i*40
        if i == 1:
            s.append(f'<circle cx="{cx}" cy="{y+478}" r="15" fill="none" stroke="{P["accent"]}" stroke-width="2"/>')
        s.append(f'<circle cx="{cx}" cy="{y+478}" r="11" fill="{c}" stroke="{P["line"]}"/>')
    s.append(r(x+372, y+464, 120, 30, "accent", 8, P=P))
    s.append(t(x+432, y+484, "Add to favorites", 11.5, "#FFFFFF", 600, anchor="middle", P=P))
    s.append("</svg>")
    return "\n".join(s)

def mock_home(P):
    W, H = 1440, 900
    b = [r(0, 45, W, H-45, "bg", 0, P=P)]
    b.append(t(W//2, 190, "Good afternoon, Professor.", 40, "ink", 400, SERIF, anchor="middle", P=P))
    b.append(t(W//2, 226, "Pick up where you left off, or open something new.", 15, "ink2", anchor="middle", P=P))
    sx, sw = (W-640)//2, 640
    b.append(r(sx, 256, sw, 52, "surf", 14, "line", P=P))
    b.append(f'<g transform="translate({sx+18},274)" stroke="{P["ink2"]}" stroke-width="1.6" fill="none">{G["search"]}</g>')
    b.append(t(sx+48, 288, "Search recent files, commands, or text inside documents", 14, "ink2", P=P))
    b.append(r(sx+sw-62, 268, 48, 28, "surf2", 7, P=P)); b.append(t(sx+sw-38, 287, "Ctrl K", 11, "ink2", 600, anchor="middle", P=P))
    b.append(r(sx, 326, 150, 40, "accent", 10, P=P)); b.append(t(sx+75, 351, "Open file", 13.5, "#FFFFFF", 600, anchor="middle", P=P))
    b.append(r(sx+160, 326, 210, 40, "surf", 10, "line", P=P)); b.append(t(sx+265, 351, "Resume bishop-prml-ch3", 13, "ink", 500, anchor="middle", P=P))
    b.append(t(120, 430, "Recent", 18, "ink", 600, SERIF, P=P))
    b.append(t(W-120, 430, "Grid  &#183;  List", 12.5, "ink2", anchor="end", P=P))
    names = ["bishop-prml-ch3.pdf", "lecture-07-notes.pdf", "thesis-draft-v4.pdf", "nature-2026-0412.pdf", "syllabus-fall26.pdf"]
    meta = ["p. 12 / 148 &#183; 2 h ago", "p. 31 / 40 &#183; yesterday", "p. 4 / 210 &#183; Mon", "p. 1 / 18 &#183; Sep 29", "p. 2 / 6 &#183; Sep 22"]
    for i, nm in enumerate(names):
        cx = 120 + i*246
        b.append(r(cx, 452, 226, 300, "surf", 14, "line", P=P))
        b.append(r(cx+14, 466, 198, 210, "page", 6, "line", P=P))
        for k in range(9):
            b.append(r(cx+30, 486+k*18, 160-(k*23 % 60), 5, "ink2", 2, P=P, op=.2))
        if i in (0, 1):
            b.append(r(cx+14, 672, 198*(.35 if i == 0 else .8), 4, "accent", 2, P=P))
        b.append(t(cx+16, 704, nm, 13, "ink", 500, P=P))
        b.append(t(cx+16, 726, meta[i], 11.5, "ink2", P=P))
    return window(P, "home", "\n".join(b), W, H)

files = {
  "01-reader-light-annotating.svg": mock_reader(LIGHT, "reader"),
  "02-reader-dark-annotating.svg": mock_reader(DARK, "reader", True),
  "03-pen-settings-panel.svg": mock_pen_panel(LIGHT),
  "04-home-light.svg": mock_home(LIGHT),
  "05-home-dark.svg": mock_home(DARK),
}
for fn, svg in files.items():
    p = os.path.join(OUT, fn)
    open(p, "w", encoding="utf-8").write(svg)
    cairosvg.svg2png(bytestring=svg.encode(), write_to=p.replace(".svg", ".png"))
print("done")
