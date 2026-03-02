# ESPHome Online Image — Grayscale JPEG Fix

This repository provides a patched `online_image` component for [ESPHome](https://esphome.io/) that fixes decoding of grayscale JPEG images.

## The Problem

The upstream `online_image` component unconditionally sets the JPEGDEC pixel type to `RGB8888`, which assumes all JPEG images contain colour data. When a grayscale JPEG (8-bit, single channel) is decoded with this setting, the pixel data is misinterpreted — each single-byte grayscale value is read as part of a 4-byte RGBA tuple, producing corrupted output (wrong colours, shifted pixels, or crashes).

## The Fix

The fix is contained entirely in **`jpeg_image.cpp`** — all other files are identical to the upstream ESPHome `release` branch.

Two changes were made:

### 1. Pixel type selection in `JpegDecoder::decode()`

The decoder now checks the image's bits-per-pixel before choosing a pixel type:

```cpp
int bpp = this->jpeg_.getBpp();
if (bpp <= 8) {
  this->jpeg_.setPixelType(EIGHT_BIT_GRAYSCALE);
} else {
  this->jpeg_.setPixelType(RGB8888);
}
```

**Upstream behaviour:** Always calls `this->jpeg_.setPixelType(RGB8888)` regardless of the source image format.

### 2. Grayscale pixel handling in `draw_callback()`

The draw callback now branches on `jpeg->iBpp` to correctly read pixel data for each format:

```cpp
if (jpeg->iBpp == 8) {
  auto *bytes = reinterpret_cast<uint8_t *>(jpeg->pPixels);
  uint8_t gray = bytes[position++];
  color = Color(gray, gray, gray);
} else {
  auto rg = decode_value(jpeg->pPixels[position++]);
  auto ba = decode_value(jpeg->pPixels[position++]);
  color = Color(rg[1], rg[0], ba[1], ba[0]);
}
```

**Upstream behaviour:** Always reads two 16-bit values per pixel (`decode_value` x2), which is correct for RGB8888 but reads out of bounds for 8-bit grayscale data.

## Usage

Add this as an external component in your ESPHome YAML:

```yaml
external_components:
  - source: github://jtenniswood/image-experiment@main
    components: [online_image]
```

Then use `online_image` as normal:

```yaml
online_image:
  - url: "https://example.com/photo.jpeg"
    format: JPEG
    id: my_image
    resize: 800x800
    type: RGB565
    byte_order: little_endian
    on_download_finished:
      - logger.log: "Image downloaded"
```

Both colour and grayscale JPEGs will decode correctly.

## Compatibility

This component is based on the ESPHome `release` branch and maintains full API compatibility. It can be used as a drop-in replacement — no configuration changes are needed beyond adding the `external_components` entry.
