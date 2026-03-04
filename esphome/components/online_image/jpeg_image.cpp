#include "jpeg_image.h"
#ifdef USE_ONLINE_IMAGE_JPEG_SUPPORT

#include "esphome/components/display/display_buffer.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "online_image.h"
static const char *const TAG = "online_image.jpeg";

namespace esphome {
namespace online_image {

static int draw_callback(JPEGDRAW *jpeg) {
  ImageDecoder *decoder = (ImageDecoder *) jpeg->pUser;
  App.feed_wdt();

  if (jpeg->iBpp == 16) {
    decoder->draw_rgb565_block(jpeg->x, jpeg->y, jpeg->iWidth, jpeg->iHeight,
                               reinterpret_cast<const uint8_t *>(jpeg->pPixels));
    return 1;
  }

  size_t position = 0;
  size_t height = static_cast<size_t>(jpeg->iHeight);
  size_t width = static_cast<size_t>(jpeg->iWidth);
  for (size_t y = 0; y < height; y++) {
    for (size_t x = 0; x < width; x++) {
      Color color;
      if (jpeg->iBpp == 8) {
        auto *bytes = reinterpret_cast<uint8_t *>(jpeg->pPixels);
        uint8_t gray = bytes[position++];
        color = Color(gray, gray, gray);
      } else {
        auto rg = decode_value(jpeg->pPixels[position++]);
        auto ba = decode_value(jpeg->pPixels[position++]);
        color = Color(rg[1], rg[0], ba[1], ba[0]);
      }
      if (!decoder) {
        ESP_LOGE(TAG, "Decoder pointer is null!");
        return 0;
      }
      decoder->draw(jpeg->x + x, jpeg->y + y, 1, 1, color);
    }
  }
  return 1;
}

int JpegDecoder::prepare(size_t download_size) {
  ImageDecoder::prepare(download_size);
  auto size = this->image_->resize_download_buffer(download_size);
  if (size < download_size) {
    ESP_LOGE(TAG, "Download buffer resize failed!");
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  return 0;
}

int HOT JpegDecoder::decode(uint8_t *buffer, size_t size) {
  if (size < this->download_size_) {
    ESP_LOGV(TAG, "Download not complete. Size: %d/%d", size, this->download_size_);
    return 0;
  }

  if (!this->jpeg_.openRAM(buffer, size, draw_callback)) {
    ESP_LOGE(TAG, "Could not open image for decoding: %d", this->jpeg_.getLastError());
    return DECODE_ERROR_INVALID_TYPE;
  }
  auto jpeg_type = this->jpeg_.getJPEGType();
  if (jpeg_type == JPEG_MODE_INVALID) {
    ESP_LOGE(TAG, "Unsupported JPEG image");
    return DECODE_ERROR_INVALID_TYPE;
  } else if (jpeg_type == JPEG_MODE_PROGRESSIVE) {
    ESP_LOGE(TAG, "Progressive JPEG images not supported");
    return DECODE_ERROR_INVALID_TYPE;
  }
  int bpp = this->jpeg_.getBpp();
  int src_w = this->jpeg_.getWidth();
  int src_h = this->jpeg_.getHeight();
  ESP_LOGD(TAG, "Image size: %d x %d, bpp: %d", src_w, src_h, bpp);

  this->jpeg_.setUserPointer(this);
  if (bpp <= 8) {
    this->jpeg_.setPixelType(EIGHT_BIT_GRAYSCALE);
  } else if (this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB565) {
    this->jpeg_.setPixelType(this->image_->is_big_endian() ? RGB565_BIG_ENDIAN : RGB565_LITTLE_ENDIAN);
  } else {
    this->jpeg_.setPixelType(RGB8888);
  }

  int decode_options = 0;
  int out_w = src_w;
  int out_h = src_h;
  int target_w = this->image_->get_fixed_width();
  int target_h = this->image_->get_fixed_height();
  if (target_w > 0 && target_h > 0) {
    if (src_w / 8 >= target_w && src_h / 8 >= target_h) {
      decode_options = JPEG_SCALE_EIGHTH;
      out_w = src_w / 8;
      out_h = src_h / 8;
    } else if (src_w / 4 >= target_w && src_h / 4 >= target_h) {
      decode_options = JPEG_SCALE_QUARTER;
      out_w = src_w / 4;
      out_h = src_h / 4;
    } else if (src_w / 2 >= target_w && src_h / 2 >= target_h) {
      decode_options = JPEG_SCALE_HALF;
      out_w = src_w / 2;
      out_h = src_h / 2;
    }
    if (decode_options) {
      ESP_LOGD(TAG, "Using decoder downscale: %dx%d -> %dx%d", src_w, src_h, out_w, out_h);
    }
  }

  if (!this->set_size(out_w, out_h)) {
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  if (!this->jpeg_.decode(0, 0, decode_options)) {
    ESP_LOGE(TAG, "Error while decoding.");
    this->jpeg_.close();
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }
  this->decoded_bytes_ = size;
  this->jpeg_.close();
  return size;
}

}  // namespace online_image
}  // namespace esphome

#endif  // USE_ONLINE_IMAGE_JPEG_SUPPORT
