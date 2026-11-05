"""Render the C++ NanoVG draw-command fixture, NOT an emulator screenshot."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


def render(source, font_path):
    fixture = json.loads(source.read_text(encoding="utf-8"))
    size = (fixture["width"], fixture["height"])
    image = Image.new("RGBA", size, (15, 23, 35, 255))
    if fixture.get("checker_background"):
        # Neutral synthetic scene for checking transparent overlays, not a
        # captured game frame or an emulator/hardware validation claim.
        background = ImageDraw.Draw(image)
        for y in range(0, size[1], 80):
            for x in range(0, size[0], 80):
                color = (76, 112, 90, 255) if (x // 80 + y // 80) % 2 else (152, 171, 155, 255)
                background.rectangle((x, y, x + 79, y + 79), fill=color)
    fonts = {}
    for command in fixture["commands"]:
        layer = Image.new("RGBA", size)
        draw = ImageDraw.Draw(layer)
        color = tuple(max(0, min(255, round(c * 255))) for c in command["color"])
        x, y = command["x"], command["y"]
        if command["kind"] == "round_rect":
            box = (x, y, x + command["w"], y + command["h"])
            radius = min(command["radius"], command["w"] / 2, command["h"] / 2)
            if "stroke" in command:
                draw.rounded_rectangle(box, radius, outline=color, width=max(1, round(command["stroke"])))
            else:
                draw.rounded_rectangle(box, radius, fill=color)
        elif command["kind"] == "text":
            pixels = max(1, round(command["size"]))
            if pixels not in fonts:
                fonts[pixels] = ImageFont.truetype(font_path, pixels)
            horizontal = "m" if command["align"] & 2 else "r" if command["align"] & 4 else "l"
            draw.text((x, y), command["text"], fill=color, font=fonts[pixels], anchor=horizontal + "m")
        else:
            raise ValueError("Unknown draw command")
        if command["clip"] is not None:
            cx, cy, cw, ch = command["clip"]
            mask = Image.new("L", size)
            if cw > 0 and ch > 0:
                ImageDraw.Draw(mask).rectangle((cx, cy, cx + cw, cy + ch), fill=255)
            layer.putalpha(ImageChops.multiply(layer.getchannel("A"), mask))
        image = Image.alpha_composite(image, layer)
    destination = source.with_suffix(".png")
    image.convert("RGB").save(destination)
    print(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--font", default="C:/Windows/Fonts/msyh.ttc")
    options = parser.parse_args()
    for source in sorted(options.directory.glob("*.json")):
        render(source, options.font)
