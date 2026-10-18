"""Generate the development launcher's text-only icon (requires Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import os

font = os.environ.get('WOS_ICON_FONT', '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')
image = Image.new('RGB', (512, 512), '#0c1018')
draw = ImageDraw.Draw(image)
draw.rectangle((0, 0, 511, 17), fill='#d63748')
draw.rectangle((0, 494, 511, 511), fill='#d63748')
for text, y, size, color in (
    ('WEB OF', 130, 65, '#ffffff'),
    ('SHADOWS', 215, 65, '#ffffff'),
    ('DEVELOPMENT', 365, 26, '#e8a1aa'),
):
    face = ImageFont.truetype(font, size)
    draw.text((256, y), text, font=face, fill=color, anchor='mt')
destination = Path(__file__).resolve().parents[1] / 'sce_sys/icon0.png'
image.save(destination)
print(destination)
