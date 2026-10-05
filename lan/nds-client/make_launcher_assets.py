"""Encode the supplied logo for DS banners and the two supported launchers.

This only resamples/quantizes the existing artwork; it does not redraw it.
Requires Pillow. Run with the original icon PNG as the first argument.
"""
from pathlib import Path
import struct
import sys
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent
assets = root / 'assets'
assets.mkdir(exist_ok=True)
source = Image.open(sys.argv[1]).convert('RGB')

# Flood only the exterior background: the white stripe inside stays opaque.
mask = Image.new('L', source.size, 0)
ImageDraw.floodfill(source, (0, 0), (255, 0, 255), thresh=65)
for y in range(source.height):
    for x in range(source.width):
        if source.getpixel((x, y)) == (255, 0, 255):
            mask.putpixel((x, y), 255)
source = Image.open(sys.argv[1]).convert('RGB')
small = source.resize((32, 32), Image.LANCZOS)
exterior = mask.resize((32, 32), Image.NEAREST)
palette = [255,0,255, 39,49,61, 78,98,115, 120,150,172,
           164,191,209, 201,221,233, 226,239,245, 255,255,255,
           161,0,10, 212,0,12, 243,12,29, 255,54,66,
           0,139,195, 0,168,220, 0,193,229, 50,218,239]
reference = Image.new('P', (1, 1))
reference.putpalette(palette[3:] + [255,255,255]*(256-15))
quantized = small.quantize(palette=reference, dither=Image.NONE)
indices = [0 if exterior.getpixel((x, y)) else quantized.getpixel((x, y))+1
           for y in range(32) for x in range(32)]
# Write a real uncompressed 4-bit BMP (Pillow's P-mode BMP is 8-bit).
pixels = bytes((indices[y*32+x] << 4) | indices[y*32+x+1]
               for y in reversed(range(32)) for x in range(0, 32, 2))
offset = 14 + 40 + 16*4
header = struct.pack('<2sIHHI', b'BM', offset+len(pixels), 0, 0, offset)
header += struct.pack('<IiiHHIIiiII', 40, 32, 32, 1, 4, 0, len(pixels), 0, 0, 16, 16)
colors = b''.join(bytes((palette[i+2], palette[i+1], palette[i], 0)) for i in range(0, 48, 3))
(assets/'icon.bmp').write_bytes(header+colors+pixels)
preview = Image.new('P', (32, 32))
preview.putpalette(palette + [0]*(768-len(palette)))
preview.putdata(indices)
preview.save(assets/'icon.png', transparency=0)
rgba = preview.convert('RGBA')
rgba.putalpha(Image.eval(exterior, lambda value: 255-value))
rgba.resize((256,256), Image.NEAREST).save(root/'dist/launcher-icon-preview.png')

# Pico renders only the first 106 columns of its 128x96 indexed BMP.
cover = Image.new('RGB', (128, 96), 'white')
cover.paste(source.resize((96, 96), Image.LANCZOS), (5, 0))
cover.quantize(colors=256, dither=Image.NONE).save(assets/'pico-cover.bmp')
boxart = Image.new('RGB', (128, 115), 'white')
boxart.paste(source.resize((115, 115), Image.LANCZOS), (6, 0))
boxart.save(assets/'twilight-boxart.png', optimize=True)
print('Created DS 32x32 4bpp icon, Pico 128x96 8bpp cover, TWiLight 128x115 PNG')
