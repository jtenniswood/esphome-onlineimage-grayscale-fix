#include "jpeg_image_hw.h"

#ifdef USE_ESP_IDF
#include "soc/soc_caps.h"
#if SOC_JPEG_CODEC_SUPPORTED

#include <cstring>
#include "driver/jpeg_decode.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "online_image.h"

static const char *const TAG = "online_image.jpeg_hw";

namespace esphome {
namespace online_image {

int HwJpegDecoder::prepare(size_t download_size) {
  ImageDecoder::prepare(download_size);
  auto size = this->image_->resize_download_buffer(download_size);
  if (size < download_size) {
    ESP_LOGE(TAG, "Download buffer resize failed!");
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  return 0;
}

int HOT HwJpegDecoder::decode(uint8_t *buffer, size_t size) {
  if (size < this->download_size_) {
    ESP_LOGV(TAG, "Download not complete. Size: %zu/%zu", size, this->download_size_);
    return 0;
  }

  jpeg_decode_memory_alloc_cfg_t tx_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
  };
  size_t tx_allocated = 0;
  uint8_t *tx_buf = (uint8_t *) jpeg_alloc_decoder_mem(size, &tx_cfg, &tx_allocated);
  if (!tx_buf) {
    ESP_LOGE(TAG, "Failed to allocate aligned input buffer (%zu bytes)", size);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  memcpy(tx_buf, buffer, size);

  jpeg_decode_picture_info_t info;
  esp_err_t err = jpeg_decoder_get_info(tx_buf, size, &info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get JPEG info: %s", esp_err_to_name(err));
    free(tx_buf);
    return DECODE_ERROR_INVALID_TYPE;
  }

  int src_w = info.width;
  int src_h = info.height;
  ESP_LOGD(TAG, "Image size: %d x %d", src_w, src_h);

  if (!this->set_size(src_w, src_h)) {
    free(tx_buf);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  int aligned_w = (src_w + 15) & ~15;
  int aligned_h = (src_h + 15) & ~15;
  size_t out_buf_size = aligned_w * aligned_h * 2;

  jpeg_decode_memory_alloc_cfg_t rx_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
  };
  size_t rx_allocated = 0;
  uint8_t *rx_buf = (uint8_t *) jpeg_alloc_decoder_mem(out_buf_size, &rx_cfg, &rx_allocated);
  if (!rx_buf) {
    ESP_LOGE(TAG, "Failed to allocate aligned output buffer (%zu bytes)", out_buf_size);
    free(tx_buf);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  jpeg_decoder_handle_t decoder_engine;
  jpeg_decode_engine_cfg_t engine_cfg = {
      .intr_priority = 0,
      .timeout_ms = 200,
  };
  err = jpeg_new_decoder_engine(&engine_cfg, &decoder_engine);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create HW decoder engine: %s", esp_err_to_name(err));
    free(tx_buf);
    free(rx_buf);
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  jpeg_decode_cfg_t decode_cfg = {
      .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
      .rgb_order = this->image_->is_big_endian() ? JPEG_DEC_RGB_ELEMENT_ORDER_RGB
                                                  : JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
      .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
  };

  uint32_t decoded_size = 0;
  err = jpeg_decoder_process(decoder_engine, &decode_cfg, tx_buf, size, rx_buf, rx_allocated, &decoded_size);

  if (err == ESP_OK) {
    App.feed_wdt();
    for (int y = 0; y < src_h; y++) {
      this->draw_rgb565_block(0, y, src_w, 1, rx_buf + y * aligned_w * 2);
    }
    ESP_LOGD(TAG, "Hardware JPEG decode complete (%u bytes output)", decoded_size);
  } else {
    ESP_LOGE(TAG, "Hardware JPEG decode failed: %s", esp_err_to_name(err));
  }

  jpeg_del_decoder_engine(decoder_engine);
  free(tx_buf);
  free(rx_buf);

  if (err != ESP_OK) {
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  this->decoded_bytes_ = size;
  return size;
}

}  // namespace online_image
}  // namespace esphome

#endif  // SOC_JPEG_CODEC_SUPPORTED
#endif  // USE_ESP_IDF
