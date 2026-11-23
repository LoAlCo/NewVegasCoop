# Draws the ping icons into data/textures/interface/nvcoop: plain shapes in white, which the game tints with the HUD's
# colour. Uncompressed 32 bit DDS with mipmaps, so they stay clean at the small size they're shown.
import math, os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'data', 'textures', 'interface', 'nvcoop')
SIZE = 64
SAMPLES = 4


def spot(x, y):
    d = abs(x - 32) + abs(y - 32)
    return 11 < d <= 26


def enemy(x, y):
    head = ((x - 32) / 21) ** 2 + ((y - 26) / 20) ** 2 <= 1
    jaw = 21 <= x <= 43 and 38 <= y <= 56
    eye = (x - 23) ** 2 + (y - 28) ** 2 <= 36 or (x - 41) ** 2 + (y - 28) ** 2 <= 36
    nose = 34 <= y <= 42 and abs(x - 32) <= (y - 34) / 2
    return (head or jaw) and not eye and not nose


def loot(x, y):
    dx, dy = x - 32, y - 32
    r = math.hypot(dx, dy)
    turn = (math.atan2(dy, dx) / (2 * math.pi) * 16) % 1
    edge = 23 + 6 * abs(2 * turn - 1)
    return 14 < r <= edge


def tool(x, y):
    ax, ay, bx, by = 40, 24, 13, 53
    t = max(0, min(1, ((x - ax) * (bx - ax) + (y - ay) * (by - ay)) / ((bx - ax) ** 2 + (by - ay) ** 2)))
    handle = math.hypot(x - ax - t * (bx - ax), y - ay - t * (by - ay)) <= 5.7
    head = math.hypot(x - 45, y - 19) <= 14
    along = ((x - 45) + (19 - y)) / math.sqrt(2)
    across = ((x - 45) + (y - 19)) / math.sqrt(2)
    notch = along > 1 and abs(across) <= 6
    return handle or (head and not notch)


def friend(x, y):
    head = math.hypot(x - 32, y - 19) <= 12
    body = y <= 57 and ((x - 32) / 23) ** 2 + ((y - 57) / 22) ** 2 <= 1
    return head or body


ICONS = {'ping_spot': spot, 'ping_enemy': enemy, 'ping_loot': loot, 'ping_tool': tool, 'ping_friend': friend}


def draw(shape):
    alpha = []
    for py in range(SIZE):
        for px in range(SIZE):
            hits = sum(shape(px + (sx + 0.5) / SAMPLES, py + (sy + 0.5) / SAMPLES)
                       for sy in range(SAMPLES) for sx in range(SAMPLES))
            alpha.append(255 * hits / (SAMPLES * SAMPLES))
    return alpha


def half(alpha, size):
    return [(alpha[y * size + x] + alpha[y * size + x + 1] + alpha[(y + 1) * size + x] + alpha[(y + 1) * size + x + 1]) / 4
            for y in range(0, size, 2) for x in range(0, size, 2)]


def dds(alpha):
    levels = []
    size = SIZE
    while True:
        levels.append(alpha)
        if size == 1:
            break
        alpha = half(alpha, size)
        size //= 2
    # Caps, height, width, pitch, pixel format, mipmap count, complex + mipmap + texture.
    head = struct.pack('<4s7I44x8I4I12x', b'DDS ', 124, 0x2100F, SIZE, SIZE, SIZE * 4, 0, len(levels),
                       32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 0x401008, 0, 0, 0)
    body = bytearray()
    for level in levels:
        for a in level:
            body += bytes((255, 255, 255, int(a + 0.5)))
    return head + bytes(body)


os.makedirs(OUT, exist_ok=True)
for name, shape in ICONS.items():
    with open(os.path.join(OUT, name + '.dds'), 'wb') as f:
        f.write(dds(draw(shape)))
    print(name)
