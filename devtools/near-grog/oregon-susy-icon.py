# The Oregon Trail (Susy Edition)'s app icon: an original 64x64 pixel
# drawing of a covered wagon with an S on its canvas, scaled up
from PIL import Image, ImageDraw
import math
N = 64
im = Image.new('RGB', (N, N))
px = im.load()
SKY_TOP, SKY_BOT = (40, 110, 200), (140, 200, 250)
for y in range(N):
    t = y / 40
    c = tuple(int(SKY_TOP[i] + (SKY_BOT[i] - SKY_TOP[i]) * min(t, 1)) for i in range(3))
    for x in range(N):
        px[x, y] = c
d = ImageDraw.Draw(im)
# prairie
d.rectangle((0, 46, N, N), fill=(40, 150, 40))
d.rectangle((0, 46, N, 47), fill=(80, 190, 60))
for x in range(0, N, 5):
    px[x, 49 + (x * 7) % 9] = (30, 110, 30)
# canvas: a white arch over the bed, grey hoops
WHITE, GREY, DARK = (255, 255, 255), (170, 170, 170), (60, 60, 60)
cx0, cx1, top, base = 12, 52, 12, 34
for x in range(cx0, cx1 + 1):
    u = (x - cx0) / (cx1 - cx0)
    h = int(math.sqrt(max(0, 1 - (2 * u - 1) ** 2 * 0.55)) * (base - top))
    for y in range(base - h, base + 1):
        px[x, y] = WHITE
    px[x, base - h] = DARK
for hx in (20, 28, 36, 44):
    for y in range(top, base + 1):
        if px[hx, y] == WHITE:
            px[hx, y] = GREY
# outline the canvas's ends
for y in range(top, base + 1):
    for x in range(cx0, cx1 + 1):
        if px[x, y] == WHITE and (px[x - 1, y] not in (WHITE, GREY) or px[x + 1, y] not in (WHITE, GREY)):
            px[x, y] = DARK
# front opening
d.ellipse((13, 20, 19, 33), fill=(30, 30, 30))
# the S (bold, 13 x 17), dark red
S = ["..#######..",
     ".#########.",
     "###.....###",
     "###........",
     "####.......",
     ".########..",
     "..########.",
     ".......####",
     "........###",
     "###.....###",
     ".#########.",
     "..#######.."]
sx, sy = 27, 17
for r, row in enumerate(S):
    for c, ch in enumerate(row):
        if ch == '#':
            px[sx + c, sy + r] = (170, 20, 20)
# wooden bed
d.rectangle((10, 34, 54, 41), fill=(140, 80, 30))
d.rectangle((10, 34, 54, 35), fill=(180, 110, 50))
d.line((10, 41, 54, 41), fill=(80, 45, 15))
for x in range(14, 54, 8):
    d.line((x, 36, x, 40), fill=(110, 60, 20))
# wheels
def wheel(cx, cy, r):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            dd = math.hypot(x - cx, y - cy)
            if r - 1.6 <= dd <= r + 0.4:
                px[x, y] = (250, 190, 140)
            elif dd < r - 1.6:
                a = math.atan2(y - cy, x - cx)
                if dd < 1.6 or min(abs(math.sin(4 * a)) for _ in [0]) < 0.25:
                    px[x, y] = (110, 60, 20)
    for y in range(cy - r - 1, cy + r + 2):
        for x in range(cx - r - 1, cx + r + 2):
            if abs(math.hypot(x - cx, y - cy) - (r + 1)) < 0.5:
                px[x, y] = (70, 35, 10)
wheel(19, 44, 7)
wheel(46, 44, 7)
# tongue
d.line((3, 39, 10, 39), fill=(110, 60, 20))
big = im.resize((1024, 1024), Image.NEAREST)
big.save('dists/neargrog/oregon-susy-icon.png')
