# Makes the divider for a right aligned conversations log: a glowing line, solid on its right and fading out to the
# left, shaped to match the game's left aligned glow_tip_line_horizontal. The game can't mirror a texture, so this is
# its own file. Writes data/textures/interface/nvcoop/talk_divider_right.dds (uncompressed, 64 x 32, no mipmaps).
import os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'data', 'textures', 'interface', 'nvcoop', 'talk_divider_right.dds')

# Top to bottom: a thin bright core with a soft glow around it
ROW_ALPHA = [1, 4, 12, 24, 35, 49, 63, 85, 99, 116, 128, 140, 151, 255, 255, 255, 253, 160, 142, 124, 109, 92, 68, 52,
             34, 21, 9, 5, 2, 1, 0, 0]
ROW_GREY = [0, 8, 16, 16, 24, 32, 40, 48, 56, 68, 80, 93, 107, 251, 251, 251, 251, 101, 101, 101, 65, 50, 35, 20, 10, 5,
            0, 0, 0, 0, 0, 0]
# From the solid end to the faded one
FADE_ALPHA = [255] * 20 + [251, 251, 251, 251, 244, 244, 244, 229, 226, 212, 212, 198, 193, 181, 169, 158, 146, 127, 118,
                           100, 86, 79, 64, 49, 34, 26, 18, 14, 8, 4, 1, 1] + [0] * 12
FADE_GREY = [255] * 20 + [247, 247, 247, 247, 231, 231, 231, 231, 206, 206, 206, 167, 165, 165, 137, 137, 123, 104, 104,
                          85, 65, 65, 49, 33, 24, 18, 13, 8, 5, 2, 0, 0] + [0] * 12
WIDTH, HEIGHT = len(FADE_ALPHA), len(ROW_ALPHA)

pixels = bytearray()
for y in range(HEIGHT):
    for x in range(WIDTH):
        fade = WIDTH - 1 - x
        grey = ROW_GREY[y] * FADE_GREY[fade] // 255
        pixels += bytes((grey, grey, grey, ROW_ALPHA[y] * FADE_ALPHA[fade] // 255))

CAPS_CREATE = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8  # caps, height, width, pixel format, pitch
pixel_format = struct.pack('<8I', 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
header = struct.pack('<7I', 124, CAPS_CREATE, HEIGHT, WIDTH, WIDTH * 4, 0, 0) + bytes(44) + pixel_format
header += struct.pack('<5I', 0x1000, 0, 0, 0, 0)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'wb') as f:
    f.write(b'DDS ' + header + pixels)
print(OUT, os.path.getsize(OUT), 'bytes')
