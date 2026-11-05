# yuanshi launcher icon

The icon uses an ivory abstract launch / wing mark on a charcoal background, with
generous spacing so it remains readable in hbmenu. The master artwork was generated for
this project with OpenAI image generation and resized for the formats below.

- `icon-master.png`: 1024 × 1024 RGB source artwork.
- `../icon/icon.png`: 256 × 256 RGB PNG for RomFS and distribution.
- `../icon/icon.jpg`: 256 × 256 baseline RGB JPEG embedded by `elf2nro`.
- `../hb-app-store/switch-icon.png`: 256 × 150 store artwork.

Resize the master with Lanczos filtering. Export the JPEG at quality 95 with
4:4:4 sampling and without progressive encoding. The NRO icon must stay an opaque
256 × 256 JPEG; do not substitute a PNG or a transparent image.
