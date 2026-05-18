#pragma once

#ifdef USE_ESP32

#include "esphome/components/display/display.h"
#include "esphome/components/esp32_camera/esp32_camera.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <memory>

extern "C" {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp32s3/rom/tjpgd.h"
#else
#include "esp32/rom/tjpgd.h"
#endif
}

namespace esphome {
namespace uvc_display_preview {

class UvcDisplayPreview : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_display(display::Display *display) { this->display_ = display; }
  void set_lvgl(lvgl::LvglComponent *lvgl) { this->lvgl_ = lvgl; }
  void set_min_frame_interval(uint32_t interval_ms) { this->min_frame_interval_ms_ = interval_ms; }

  void set_enabled(bool enabled);
  bool is_enabled() const { return this->enabled_.load(); }

 protected:
  struct FrameBuffer {
    uint8_t *data{nullptr};
    size_t len{0};
    size_t capacity{0};
    uint16_t width{0};
    uint16_t height{0};
  };

  struct DecodeContext {
    UvcDisplayPreview *preview{nullptr};
    const uint8_t *input{nullptr};
    size_t input_len{0};
    size_t input_pos{0};
    uint16_t source_width{0};
    uint16_t source_height{0};
    uint16_t crop_x{0};
    uint16_t crop_y{0};
    uint16_t crop_size{0};
    uint8_t decode_scale{0};
  };

  static void task_entry_(void *arg);
  bool ensure_callback_registered_();
  bool ensure_task_running_();
  void task_loop_();
  void maybe_request_frame_();
  void maybe_log_stats_();
  bool should_accept_frame_(uint32_t now);
  void on_image_(std::shared_ptr<esp32_camera::CameraImage> image);
#ifdef USE_UVC_DISPLAY_PREVIEW_RAW_TAP
  void on_raw_image_(camera_fb_t *raw);
#endif
  bool copy_frame_(camera_fb_t *raw);
  bool ensure_frame_capacity_(size_t capacity);
  bool ensure_frame_buffer_capacity_(FrameBuffer *frame, size_t capacity);
  bool ensure_output_buffer_();
  int8_t get_free_output_buffer_index_() const;
  bool ensure_draw_buffer_();
  bool ensure_work_buffer_();
  bool ensure_scaled_buffer_(size_t size);
  bool decode_frame_(FrameBuffer *frame);
  bool decode_frame_esp_new_jpeg_(FrameBuffer *frame);
  bool decode_frame_tjpgd_(FrameBuffer *frame);
  bool transform_scaled_rgb565_(const uint8_t *scaled_buffer, uint16_t scaled_width, uint16_t scaled_height);
  void log_new_jpeg_failure_(const char *reason, int ret = 0, int detail = 0);
  void clear_pending_frame_();
  void set_lvgl_paused_(bool paused);
  void disable_preview_after_error_();

  static UINT jpeg_input_(JDEC *decoder, BYTE *buffer, UINT len);
  static UINT jpeg_output_(JDEC *decoder, void *bitmap, JRECT *rect);

  display::Display *display_{nullptr};
  lvgl::LvglComponent *lvgl_{nullptr};
  std::atomic<bool> enabled_{false};
  bool task_running_{false};
  bool ready_{false};
  bool processing_{false};
  bool decoded_frame_ready_{false};
  bool drawing_frame_{false};
  bool preview_showing_{false};
  bool lvgl_paused_{false};
  bool callback_registered_{false};
  std::atomic<bool> preview_failed_{false};
  uint32_t min_frame_interval_ms_{200};
  uint32_t next_frame_ms_{0};
  uint32_t last_request_ms_{0};
  uint32_t last_stats_ms_{0};
  uint32_t last_jpeg_ms_{0};
  uint32_t last_decode_ms_{0};
  uint32_t last_transform_ms_{0};
  uint32_t last_draw_ms_{0};
  uint32_t frames_copied_{0};
  uint32_t frames_decoded_{0};
  uint32_t frames_drawn_{0};
  uint32_t frames_new_jpeg_{0};
  uint32_t frames_tjpgd_{0};
  uint32_t frames_dropped_busy_{0};
  uint32_t stats_prev_copied_{0};
  uint32_t stats_prev_decoded_{0};
  uint32_t stats_prev_drawn_{0};
  uint32_t stats_prev_dropped_busy_{0};
  std::atomic<bool> logged_first_frame_{false};
  bool logged_new_jpeg_fallback_{false};
  bool logged_new_jpeg_failure_{false};
  bool logged_frame_too_large_{false};
  bool logged_first_request_{false};
  bool logged_first_callback_{false};
  bool logged_first_copy_{false};
  bool logged_first_draw_{false};
  bool logged_invalid_frame_{false};
  bool logged_busy_drop_{false};

  SemaphoreHandle_t mutex_{nullptr};
  TaskHandle_t task_handle_{nullptr};
  FrameBuffer pending_frame_{};
  FrameBuffer processing_frame_{};
  bool has_pending_frame_{false};
  int8_t ready_buffer_index_{-1};
  int8_t drawing_buffer_index_{-1};
  int8_t decoding_buffer_index_{-1};
  uint8_t *output_buffers_[2]{nullptr, nullptr};
  uint8_t *output_buffer_{nullptr};
  uint8_t *draw_buffer_{nullptr};
  uint8_t *work_buffer_{nullptr};
  uint8_t *scaled_buffer_{nullptr};
  size_t scaled_buffer_size_{0};
};

}  // namespace uvc_display_preview
}  // namespace esphome

#endif  // USE_ESP32
