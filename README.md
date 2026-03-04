# ESPHome Online Image — Grayscale Fix & JPEG Performance

This repository provides a patched `online_image` component for [ESPHome](https://esphome.io/) that fixes decoding of grayscale JPEG images and adds significant performance optimisations for JPEG loading.

## Changes from Upstream

All changes are within the `online_image` component. The upstream base is the ESPHome 2026.2.1 `release` branch.

### 1. Grayscale JPEG fix

The upstream component unconditionally sets the JPEGDEC pixel type to `RGB8888`, which assumes all JPEG images contain colour data. A grayscale JPEG (8-bit, single channel) decoded with this setting produces corrupted output because each single-byte grayscale value is read as part of a 4-byte RGBA tuple.

**Modified file:** `jpeg_image.cpp`

- The decoder now checks bits-per-pixel before choosing a pixel type — `EIGHT_BIT_GRAYSCALE` for `bpp <= 8`, otherwise `RGB565` or `RGB8888` depending on the target image type.
- The draw callback branches on `jpeg->iBpp` to correctly read 8-bit grayscale pixels as single bytes.

### 2. Direct RGB565 output

When the target image type is `RGB565`, the software decoder now requests `RGB565_LITTLE_ENDIAN` or `RGB565_BIG_ENDIAN` directly from JPEGDEC instead of decoding to `RGB8888` and converting per-pixel with `color_to_565()`.

**Modified file:** `jpeg_image.cpp`

### 3. Bulk RGB565 block copy

A new `draw_rgb565_block()` method on `ImageDecoder` writes decoded MCU rows directly into the image buffer using `memcpy` at 1:1 scale. When the image is being scaled, it falls back to per-pixel copy.

**Modified files:** `image_decoder.h`, `image_decoder.cpp`

### 4. JPEGDEC built-in downscaling

When `resize` dimensions are set and the source image is significantly larger than the target, the decoder uses JPEGDEC's built-in downscaling (`JPEG_SCALE_HALF`, `JPEG_SCALE_QUARTER`, `JPEG_SCALE_EIGHTH`) to reduce decode computation before the image reaches the draw callback.

**Modified file:** `jpeg_image.cpp`

### 5. Aspect-ratio-preserving resize

When fixed `resize` dimensions are configured, the image is now fitted within the bounding box using a uniform scale factor rather than being stretched to fill the exact dimensions.

**Modified file:** `online_image.cpp`

### 6. ESP32-P4 hardware JPEG decoder

On chips with a hardware JPEG codec (currently the ESP32-P4), a new `HwJpegDecoder` class uses the ESP-IDF `esp_driver_jpeg` API to decode directly to RGB565 in hardware — bypassing the software JPEGDEC library entirely. The hardware path is selected automatically when the target image type is `RGB565`.

**New files:** `jpeg_image_hw.h`, `jpeg_image_hw.cpp`

### 7. Larger download buffer limit

The `buffer_size` configuration upper limit has been raised from 64 KB to 512 KB for devices with large PSRAM, allowing full-image buffering of bigger JPEGs.

**Modified file:** `__init__.py`

### 8. Watchdog feed in draw callback

The JPEG draw callback now calls `App.feed_wdt()` on each MCU block to prevent watchdog resets when decoding large images.

**Modified file:** `jpeg_image.cpp`

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

Both colour and grayscale JPEGs will decode correctly. On ESP32-P4, hardware JPEG decoding is used automatically for RGB565 images.

## Compatibility

This component is based on the ESPHome 2026.2.1 `release` branch and maintains full API compatibility. It can be used as a drop-in replacement — no configuration changes are needed beyond adding the `external_components` entry.
