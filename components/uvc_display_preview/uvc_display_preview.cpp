#include "uvc_display_preview.h"

#ifdef USE_ESP32

#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include "esp_heap_caps.h"
#if defined(USE_UVC_DISPLAY_PREVIEW_ESP_NEW_JPEG) && __has_include("esp_jpeg_dec.h")
#include "esp_jpeg_dec.h"
#define UVC_DISPLAY_PREVIEW_HAS_ESP_NEW_JPEG 1
#endif
}

namespace esphome {
namespace uvc_display_preview {

static const char *const TAG = "uvc_display_preview";
static constexpr uint16_t PREVIEW_SIZE = 240;
static constexpr uint16_t DRAW_STRIPE_HEIGHT = 16;
static constexpr size_t MAX_JPEG_FRAME_SIZE = 256 * 1024;
static constexpr size_t MAX_NEW_JPEG_OUTPUT_SIZE = 2 * 1024 * 1024;
static constexpr size_t TJPGD_WORK_SIZE = 32 * 1024;

#ifndef UVC_DISPLAY_PREVIEW_HAS_ESP_NEW_JPEG
#define UVC_DISPLAY_PREVIEW_HAS_ESP_NEW_JPEG 0
#endif

static uint8_t choose_decode_scale(uint16_t width, uint16_t height) {
  const uint16_t crop_size = std::min(width, height);
  if (crop_size / 2 >= PREVIEW_SIZE)
    return 1;
  return 0;
}

void UvcDisplayPreview::setup() {
  if (this->display_ == nullptr || this->lvgl_ == nullptr) {
    ESP_LOGE(TAG, "Display or LVGL is not configured");
    this->preview_failed_.store(true);
    return;
  }

  this->mutex_ = xSemaphoreCreateMutex();
  if (this->mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create mutex");
    this->preview_failed_.store(true);
    return;
  }

  if (esp32_camera::global_esp32_camera == nullptr) {
    ESP_LOGE(TAG, "No global camera is available");
    this->preview_failed_.store(true);
    return;
  }

  this->ready_ = true;
#if UVC_DISPLAY_PREVIEW_HAS_ESP_NEW_JPEG
  ESP_LOGI(TAG, "Preview component ready (ESP_NEW_JPEG compiled in)");
#else
  ESP_LOGW(TAG, "Preview component ready (ESP_NEW_JPEG not compiled in)");
#endif
}

void UvcDisplayPreview::loop() {
  if (!this->ready_)
    return;

  if (this->enabled_.load()) {
    this->maybe_request_frame_();
    this->maybe_log_stats_();
  }

  bool draw_frame = false;
  int8_t draw_buffer_index = -1;
  if (this->mutex_ != nullptr && xSemaphoreTake(this->mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
    draw_frame = this->enabled_.load() && this->ready_buffer_index_ >= 0 && !this->drawing_frame_;
    if (draw_frame) {
      draw_buffer_index = this->ready_buffer_index_;
      this->ready_buffer_index_ = -1;
      this->decoded_frame_ready_ = false;
      this->drawing_buffer_index_ = draw_buffer_index;
      this->drawing_frame_ = true;
    }
    xSemaphoreGive(this->mutex_);
  }

  if (draw_frame) {
    this->set_lvgl_paused_(true);
    const uint32_t draw_start_ms = millis();
    uint8_t *draw_source = (draw_buffer_index >= 0 && draw_buffer_index < 2) ? this->output_buffers_[draw_buffer_index] : nullptr;
    if (draw_source != nullptr && this->ensure_draw_buffer_()) {
      for (uint16_t y = 0; y < PREVIEW_SIZE; y += DRAW_STRIPE_HEIGHT) {
        const uint16_t stripe_height = std::min<uint16_t>(DRAW_STRIPE_HEIGHT, PREVIEW_SIZE - y);
        const size_t stripe_size = static_cast<size_t>(PREVIEW_SIZE) * stripe_height * 2;
        memcpy(this->draw_buffer_, draw_source + static_cast<size_t>(y) * PREVIEW_SIZE * 2, stripe_size);
        this->display_->draw_pixels_at(0, y, PREVIEW_SIZE, stripe_height, this->draw_buffer_,
                                       display::COLOR_ORDER_RGB, display::COLOR_BITNESS_565, true);
      }
      this->preview_showing_ = true;
      this->last_draw_ms_ = millis() - draw_start_ms;
      this->frames_drawn_++;
      if (!this->logged_first_draw_) {
        ESP_LOGI(TAG, "First preview draw complete: %ums", static_cast<unsigned>(this->last_draw_ms_));
        this->logged_first_draw_ = true;
      }
    } else {
      this->disable_preview_after_error_();
      this->set_lvgl_paused_(false);
      this->preview_showing_ = false;
    }

    if (this->mutex_ != nullptr && xSemaphoreTake(this->mutex_, portMAX_DELAY) == pdTRUE) {
      this->drawing_buffer_index_ = -1;
      this->drawing_frame_ = false;
      xSemaphoreGive(this->mutex_);
    }
  }

  if (!this->enabled_.load() && this->preview_showing_) {
    this->set_lvgl_paused_(false);
    this->preview_showing_ = false;
  }
}

void UvcDisplayPreview::dump_config() {
  ESP_LOGCONFIG(TAG, "UVC Display Preview:");
  ESP_LOGCONFIG(TAG, "  Max FPS: %.1f", this->min_frame_interval_ms_ == 0 ? 0.0f : 1000.0f / this->min_frame_interval_ms_);
#if UVC_DISPLAY_PREVIEW_HAS_ESP_NEW_JPEG
  ESP_LOGCONFIG(TAG, "  ESP_NEW_JPEG: compiled in");
#else
  ESP_LOGCONFIG(TAG, "  ESP_NEW_JPEG: not compiled in");
#endif
}

float UvcDisplayPreview::get_setup_priority() const { return setup_priority::LATE; }

void UvcDisplayPreview::set_enabled(bool enabled) {
  if (enabled && !this->ready_) {
    ESP_LOGW(TAG, "Preview enable ignored: component is not ready");
    return;
  }
  if (enabled && this->preview_failed_.load()) {
    ESP_LOGW(TAG, "Preview enable ignored: component is in failed state");
    return;
  }

  if (this->enabled_.load() == enabled)
    return;

  if (enabled && (!this->ensure_frame_capacity_(MAX_JPEG_FRAME_SIZE) || !this->ensure_output_buffer_() ||
                  !this->ensure_callback_registered_() || !this->ensure_task_running_())) {
    this->preview_failed_.store(true);
    return;
  }

  this->enabled_.store(enabled);
  ESP_LOGI(TAG, "Preview %s", enabled ? "enabled" : "disabled");
  if (!enabled) {
    this->clear_pending_frame_();
    this->set_lvgl_paused_(false);
    this->preview_showing_ = false;
    this->logged_first_frame_.store(false);
    this->logged_frame_too_large_ = false;
    this->last_request_ms_ = 0;
    this->next_frame_ms_ = 0;
    this->last_stats_ms_ = 0;
    this->last_jpeg_ms_ = 0;
    this->last_decode_ms_ = 0;
    this->last_transform_ms_ = 0;
    this->last_draw_ms_ = 0;
    this->frames_copied_ = 0;
    this->frames_decoded_ = 0;
    this->frames_drawn_ = 0;
    this->frames_new_jpeg_ = 0;
    this->frames_tjpgd_ = 0;
    this->frames_dropped_busy_ = 0;
    this->stats_prev_copied_ = 0;
    this->stats_prev_decoded_ = 0;
    this->stats_prev_drawn_ = 0;
    this->stats_prev_dropped_busy_ = 0;
    this->logged_new_jpeg_fallback_ = false;
    this->logged_new_jpeg_failure_ = false;
    this->logged_first_request_ = false;
    this->logged_first_callback_ = false;
    this->logged_first_copy_ = false;
    this->logged_first_draw_ = false;
    this->logged_invalid_frame_ = false;
    this->logged_busy_drop_ = false;
  }
}

void UvcDisplayPreview::task_entry_(void *arg) {
  static_cast<UvcDisplayPreview *>(arg)->task_loop_();
  vTaskDelete(nullptr);
}

bool UvcDisplayPreview::ensure_callback_registered_() {
  if (this->callback_registered_)
    return true;

  if (esp32_camera::global_esp32_camera == nullptr) {
    ESP_LOGE(TAG, "No global camera is available");
    this->preview_failed_.store(true);
    return false;
  }

#ifdef USE_UVC_DISPLAY_PREVIEW_RAW_TAP
  esp32_camera::global_esp32_camera->add_raw_image_callback([this](camera_fb_t *fb) { this->on_raw_image_(fb); });
#else
  esp32_camera::global_esp32_camera->add_image_callback(
      [this](std::shared_ptr<esp32_camera::CameraImage> image) { this->on_image_(std::move(image)); });
#endif
  this->callback_registered_ = true;
  ESP_LOGI(TAG, "Preview camera callback registered");
  return true;
}

bool UvcDisplayPreview::ensure_task_running_() {
  if (this->task_handle_ != nullptr)
    return true;

  this->task_running_ = true;
  const BaseType_t task_result = xTaskCreatePinnedToCore(&UvcDisplayPreview::task_entry_, "uvc_lcd_preview", 12288,
                                                         this, 1, &this->task_handle_, 1);
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create preview task");
    this->task_running_ = false;
    this->preview_failed_.store(true);
    return false;
  }

  ESP_LOGI(TAG, "Preview task started");
  return true;
}

void UvcDisplayPreview::task_loop_() {
  while (this->task_running_) {
    if (!this->enabled_.load()) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    bool have_frame = false;
    int8_t output_index = -1;

    if (this->mutex_ != nullptr && xSemaphoreTake(this->mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (this->enabled_.load() && this->has_pending_frame_ && this->ready_buffer_index_ < 0) {
        output_index = this->get_free_output_buffer_index_();
      }
      if (output_index >= 0) {
        std::swap(this->processing_frame_, this->pending_frame_);
        this->has_pending_frame_ = false;
        this->processing_ = true;
        this->output_buffer_ = this->output_buffers_[output_index];
        this->decoding_buffer_index_ = output_index;
        have_frame = true;
      }
      xSemaphoreGive(this->mutex_);
    }

    if (have_frame) {
      const uint32_t decode_start_ms = millis();
      const bool decoded = this->decode_frame_(&this->processing_frame_);
      this->last_decode_ms_ = millis() - decode_start_ms;
      if (this->mutex_ != nullptr && xSemaphoreTake(this->mutex_, portMAX_DELAY) == pdTRUE) {
        if (decoded && this->enabled_.load() && output_index >= 0) {
          this->ready_buffer_index_ = output_index;
          this->decoded_frame_ready_ = true;
          this->frames_decoded_++;
        }
        this->decoding_buffer_index_ = -1;
        this->processing_ = false;
        xSemaphoreGive(this->mutex_);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(have_frame ? 1 : 5));
  }
}

void UvcDisplayPreview::maybe_request_frame_() {
#ifdef USE_UVC_DISPLAY_PREVIEW_RAW_TAP
  if (!this->logged_first_request_) {
    ESP_LOGI(TAG, "Preview using raw UVC frame tap");
    this->logged_first_request_ = true;
  }
  return;
#endif
  if (esp32_camera::global_esp32_camera == nullptr || this->min_frame_interval_ms_ == 0)
    return;

  const uint32_t now = millis();
  if (now - this->last_request_ms_ < this->min_frame_interval_ms_)
    return;

  this->last_request_ms_ = now;
  if (!this->logged_first_request_) {
    ESP_LOGI(TAG, "Preview requesting camera frames every %ums",
             static_cast<unsigned>(this->min_frame_interval_ms_));
    this->logged_first_request_ = true;
  }
  esp32_camera::global_esp32_camera->request_image(esp32_camera::IDLE);
}

void UvcDisplayPreview::maybe_log_stats_() {
  const uint32_t now = millis();
  const uint32_t elapsed_ms = this->last_stats_ms_ == 0 ? 0 : now - this->last_stats_ms_;
  if (this->last_stats_ms_ != 0 && elapsed_ms < 5000)
    return;

  const uint32_t copied_delta = this->frames_copied_ - this->stats_prev_copied_;
  const uint32_t decoded_delta = this->frames_decoded_ - this->stats_prev_decoded_;
  const uint32_t drawn_delta = this->frames_drawn_ - this->stats_prev_drawn_;
  const uint32_t busy_delta = this->frames_dropped_busy_ - this->stats_prev_dropped_busy_;
  const float copied_fps = elapsed_ms == 0 ? 0.0f : (copied_delta * 1000.0f) / elapsed_ms;
  const float drawn_fps = elapsed_ms == 0 ? 0.0f : (drawn_delta * 1000.0f) / elapsed_ms;
  const float decoded_fps = elapsed_ms == 0 ? 0.0f : (decoded_delta * 1000.0f) / elapsed_ms;

  this->last_stats_ms_ = now;
  this->stats_prev_copied_ = this->frames_copied_;
  this->stats_prev_decoded_ = this->frames_decoded_;
  this->stats_prev_drawn_ = this->frames_drawn_;
  this->stats_prev_dropped_busy_ = this->frames_dropped_busy_;

  ESP_LOGI(TAG,
           "Preview stats: copied=%u(+%u %.1ffps) decoded=%u(+%u %.1ffps) drawn=%u(+%u %.1ffps) new_jpeg=%u rom=%u "
           "busy_drop=%u(+%u) last_jpeg=%ums last_decode=%ums last_transform=%ums last_draw=%ums",
           static_cast<unsigned>(this->frames_copied_), static_cast<unsigned>(copied_delta), copied_fps,
           static_cast<unsigned>(this->frames_decoded_), static_cast<unsigned>(decoded_delta), decoded_fps,
           static_cast<unsigned>(this->frames_drawn_), static_cast<unsigned>(drawn_delta), drawn_fps,
           static_cast<unsigned>(this->frames_new_jpeg_), static_cast<unsigned>(this->frames_tjpgd_),
           static_cast<unsigned>(this->frames_dropped_busy_), static_cast<unsigned>(busy_delta),
           static_cast<unsigned>(this->last_jpeg_ms_), static_cast<unsigned>(this->last_decode_ms_),
           static_cast<unsigned>(this->last_transform_ms_),
           static_cast<unsigned>(this->last_draw_ms_));
}

bool UvcDisplayPreview::should_accept_frame_(uint32_t now) {
  if (this->min_frame_interval_ms_ == 0)
    return true;

  if (this->next_frame_ms_ == 0) {
    this->next_frame_ms_ = now + this->min_frame_interval_ms_;
    return true;
  }

  if (static_cast<int32_t>(now - this->next_frame_ms_) < 0)
    return false;

  const uint32_t periods = ((now - this->next_frame_ms_) / this->min_frame_interval_ms_) + 1;
  this->next_frame_ms_ += periods * this->min_frame_interval_ms_;
  return true;
}

void UvcDisplayPreview::on_image_(std::shared_ptr<esp32_camera::CameraImage> image) {
  if (!this->ready_ || this->preview_failed_.load() || !this->enabled_.load() || image == nullptr)
    return;

  const uint32_t now = millis();
  if (!this->should_accept_frame_(now))
    return;

  if (!this->logged_first_callback_) {
    camera_fb_t *raw = image->get_raw_buffer();
    if (raw != nullptr) {
      ESP_LOGI(TAG, "Preview camera callback: %ux%u %uB format=%d",
               static_cast<unsigned>(raw->width), static_cast<unsigned>(raw->height),
               static_cast<unsigned>(raw->len), static_cast<int>(raw->format));
    } else {
      ESP_LOGW(TAG, "Preview camera callback received null raw buffer");
    }
    this->logged_first_callback_ = true;
  }

  this->copy_frame_(image->get_raw_buffer());
}

#ifdef USE_UVC_DISPLAY_PREVIEW_RAW_TAP
void UvcDisplayPreview::on_raw_image_(camera_fb_t *raw) {
  if (!this->ready_ || this->preview_failed_.load() || !this->enabled_.load() || raw == nullptr)
    return;

  const uint32_t now = millis();
  if (!this->should_accept_frame_(now))
    return;

  if (!this->logged_first_callback_) {
    ESP_LOGI(TAG, "Preview raw frame tap: %ux%u %uB format=%d",
             static_cast<unsigned>(raw->width), static_cast<unsigned>(raw->height),
             static_cast<unsigned>(raw->len), static_cast<int>(raw->format));
    this->logged_first_callback_ = true;
  }

  this->copy_frame_(raw);
}
#endif

bool UvcDisplayPreview::copy_frame_(camera_fb_t *raw) {
  if (raw == nullptr)
    return false;

  if (raw->format != PIXFORMAT_JPEG || raw->buf == nullptr || raw->len == 0) {
    if (!this->logged_invalid_frame_) {
      ESP_LOGW(TAG, "Preview ignored invalid frame: format=%d buf=%p len=%u", static_cast<int>(raw->format), raw->buf,
               static_cast<unsigned>(raw->len));
      this->logged_invalid_frame_ = true;
    }
    return false;
  }

  if (this->mutex_ == nullptr || xSemaphoreTake(this->mutex_, 0) != pdTRUE) {
    this->frames_dropped_busy_++;
    return false;
  }

  if (!this->enabled_.load()) {
    this->frames_dropped_busy_++;
    xSemaphoreGive(this->mutex_);
    return false;
  }

  if (raw->len > this->pending_frame_.capacity) {
    if (!this->logged_frame_too_large_) {
      ESP_LOGW(TAG, "Preview frame too large: %u > %u", static_cast<unsigned>(raw->len),
               static_cast<unsigned>(this->pending_frame_.capacity));
      this->logged_frame_too_large_ = true;
    }
    xSemaphoreGive(this->mutex_);
    return false;
  }

  if (this->has_pending_frame_ && !this->logged_busy_drop_) {
    ESP_LOGI(TAG, "Preview replaced an older pending frame while decode/draw was busy");
    this->logged_busy_drop_ = true;
  }
  if (this->has_pending_frame_)
    this->frames_dropped_busy_++;

  memcpy(this->pending_frame_.data, raw->buf, raw->len);
  this->pending_frame_.len = raw->len;
  this->pending_frame_.width = raw->width;
  this->pending_frame_.height = raw->height;
  this->has_pending_frame_ = true;
  this->frames_copied_++;
  if (!this->logged_first_copy_) {
    ESP_LOGI(TAG, "First preview frame copied: %ux%u %uB", static_cast<unsigned>(raw->width),
             static_cast<unsigned>(raw->height), static_cast<unsigned>(raw->len));
    this->logged_first_copy_ = true;
  }

  xSemaphoreGive(this->mutex_);
  return true;
}

bool UvcDisplayPreview::ensure_frame_capacity_(size_t capacity) {
  return this->ensure_frame_buffer_capacity_(&this->pending_frame_, capacity) &&
         this->ensure_frame_buffer_capacity_(&this->processing_frame_, capacity);
}

bool UvcDisplayPreview::ensure_frame_buffer_capacity_(FrameBuffer *frame, size_t capacity) {
  if (frame == nullptr)
    return false;

  if (frame->capacity >= capacity)
    return true;

  uint8_t *new_buffer = nullptr;
  if (frame->data == nullptr) {
    new_buffer = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  } else {
    new_buffer = static_cast<uint8_t *>(
        heap_caps_realloc(frame->data, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (new_buffer == nullptr) {
    ESP_LOGW(TAG, "Unable to allocate %u bytes for preview frame", static_cast<unsigned>(capacity));
    return false;
  }

  frame->data = new_buffer;
  frame->capacity = capacity;
  return true;
}

bool UvcDisplayPreview::ensure_output_buffer_() {
  bool allocated = true;
  for (uint8_t i = 0; i < 2; i++) {
    if (this->output_buffers_[i] != nullptr)
      continue;

    this->output_buffers_[i] = static_cast<uint8_t *>(
        heap_caps_malloc(PREVIEW_SIZE * PREVIEW_SIZE * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->output_buffers_[i] == nullptr) {
      ESP_LOGE(TAG, "Unable to allocate preview output buffer %u", static_cast<unsigned>(i));
      allocated = false;
      break;
    }
  }

  if (this->output_buffer_ == nullptr)
    this->output_buffer_ = this->output_buffers_[0];
  return allocated;
}

int8_t UvcDisplayPreview::get_free_output_buffer_index_() const {
  for (int8_t i = 0; i < 2; i++) {
    if (i == this->ready_buffer_index_ || i == this->drawing_buffer_index_ || i == this->decoding_buffer_index_)
      continue;
    if (this->output_buffers_[i] != nullptr)
      return i;
  }
  return -1;
}

bool UvcDisplayPreview::ensure_draw_buffer_() {
  if (this->draw_buffer_ != nullptr)
    return true;

  this->draw_buffer_ = static_cast<uint8_t *>(
      heap_caps_malloc(PREVIEW_SIZE * DRAW_STRIPE_HEIGHT * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->draw_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate preview draw buffer");
    return false;
  }
  return true;
}

bool UvcDisplayPreview::ensure_work_buffer_() {
  if (this->work_buffer_ != nullptr)
    return true;

  this->work_buffer_ = static_cast<uint8_t *>(heap_caps_malloc(TJPGD_WORK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->work_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate preview JPEG work buffer");
    return false;
  }
  return true;
}

bool UvcDisplayPreview::ensure_scaled_buffer_(size_t size) {
  if (this->scaled_buffer_ != nullptr && this->scaled_buffer_size_ >= size)
    return true;

  uint8_t *new_buffer = nullptr;
  if (this->scaled_buffer_ == nullptr) {
#if UVC_DISPLAY_PREVIEW_HAS_ESP_NEW_JPEG
    new_buffer = static_cast<uint8_t *>(jpeg_calloc_align(size, 16));
#else
    new_buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
  } else {
#if UVC_DISPLAY_PREVIEW_HAS_ESP_NEW_JPEG
    jpeg_free_align(this->scaled_buffer_);
    new_buffer = static_cast<uint8_t *>(jpeg_calloc_align(size, 16));
#else
    heap_caps_free(this->scaled_buffer_);
    new_buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
  }

  if (new_buffer == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate scaled preview buffer: %u bytes", static_cast<unsigned>(size));
    this->scaled_buffer_ = nullptr;
    this->scaled_buffer_size_ = 0;
    return false;
  }

  this->scaled_buffer_ = new_buffer;
  this->scaled_buffer_size_ = size;
  return true;
}

bool UvcDisplayPreview::decode_frame_(FrameBuffer *frame) {
  if (this->decode_frame_esp_new_jpeg_(frame)) {
    this->frames_new_jpeg_++;
    return true;
  }

  if (!this->logged_new_jpeg_fallback_) {
    ESP_LOGW(TAG, "Using ROM JPEG fallback for preview");
    this->logged_new_jpeg_fallback_ = true;
  }
  const bool decoded = this->decode_frame_tjpgd_(frame);
  if (decoded)
    this->frames_tjpgd_++;
  return decoded;
}

bool UvcDisplayPreview::decode_frame_esp_new_jpeg_(FrameBuffer *frame) {
#if UVC_DISPLAY_PREVIEW_HAS_ESP_NEW_JPEG
  if (frame == nullptr || frame->data == nullptr || frame->len == 0 || this->display_ == nullptr)
    return false;
  if (!this->enabled_.load())
    return false;
  if (!this->ensure_output_buffer_()) {
    this->disable_preview_after_error_();
    return false;
  }

  jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
  config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
  config.rotate = JPEG_ROTATE_0D;
  config.block_enable = false;

  const uint16_t source_width = frame->width;
  const uint16_t source_height = frame->height;
  if (source_width < PREVIEW_SIZE || source_height < PREVIEW_SIZE) {
    this->log_new_jpeg_failure_("source too small", source_width, source_height);
    return false;
  }

  jpeg_dec_handle_t jpeg_dec = nullptr;
  jpeg_error_t ret = jpeg_dec_open(&config, &jpeg_dec);
  if (ret != JPEG_ERR_OK || jpeg_dec == nullptr) {
    this->log_new_jpeg_failure_("open", ret, jpeg_dec == nullptr ? 1 : 0);
    return false;
  }

  jpeg_dec_io_t jpeg_io = {};
  jpeg_dec_header_info_t out_info = {};
  jpeg_io.inbuf = frame->data;
  jpeg_io.inbuf_len = static_cast<int>(frame->len);

  ret = jpeg_dec_parse_header(jpeg_dec, &jpeg_io, &out_info);
  if (ret != JPEG_ERR_OK) {
    this->log_new_jpeg_failure_("header", ret, static_cast<int>(frame->len));
    jpeg_dec_close(jpeg_dec);
    return false;
  }

  if (out_info.width < PREVIEW_SIZE || out_info.height < PREVIEW_SIZE) {
    this->log_new_jpeg_failure_("decoded output too small", static_cast<int>(out_info.width),
                                static_cast<int>(out_info.height));
    jpeg_dec_close(jpeg_dec);
    return false;
  }
  const uint16_t decoded_width = out_info.width;
  const uint16_t decoded_height = out_info.height;

  int outbuf_len = 0;
  ret = jpeg_dec_get_outbuf_len(jpeg_dec, &outbuf_len);
  if (ret != JPEG_ERR_OK || outbuf_len <= 0) {
    this->log_new_jpeg_failure_("outbuf length", ret, outbuf_len);
    jpeg_dec_close(jpeg_dec);
    return false;
  }

  if (static_cast<size_t>(outbuf_len) > MAX_NEW_JPEG_OUTPUT_SIZE) {
    this->log_new_jpeg_failure_("output too large", 0, outbuf_len);
    jpeg_dec_close(jpeg_dec);
    return false;
  }

  if (!this->ensure_scaled_buffer_(static_cast<size_t>(outbuf_len))) {
    jpeg_dec_close(jpeg_dec);
    this->disable_preview_after_error_();
    return false;
  }

  jpeg_io.outbuf = this->scaled_buffer_;

  bool expected = false;
  if (this->logged_first_frame_.compare_exchange_strong(expected, true)) {
    ESP_LOGI(TAG,
             "First preview frame: source=%ux%u esp_new_jpeg decode=%ux%u center_crop=%ux%u out=%d bytes "
             "sw_rotate=90 sw_mirror_x",
             source_width, source_height, decoded_width, decoded_height, PREVIEW_SIZE, PREVIEW_SIZE, outbuf_len);
  }

  const uint32_t jpeg_start_ms = millis();
  ret = jpeg_dec_process(jpeg_dec, &jpeg_io);
  this->last_jpeg_ms_ = millis() - jpeg_start_ms;
  if (ret != JPEG_ERR_OK) {
    this->log_new_jpeg_failure_("decode", ret, jpeg_io.out_size);
    jpeg_dec_close(jpeg_dec);
    return false;
  }

  jpeg_dec_close(jpeg_dec);

  const uint32_t transform_start_ms = millis();
  const bool transformed = this->transform_scaled_rgb565_(this->scaled_buffer_, decoded_width, decoded_height);
  this->last_transform_ms_ = millis() - transform_start_ms;
  return transformed && this->enabled_.load();
#else
  this->log_new_jpeg_failure_("not compiled");
  return false;
#endif
}

bool UvcDisplayPreview::decode_frame_tjpgd_(FrameBuffer *frame) {
  if (frame == nullptr || frame->data == nullptr || frame->len == 0 || this->display_ == nullptr)
    return false;
  if (!this->enabled_.load())
    return false;
  if (!this->ensure_output_buffer_() || !this->ensure_work_buffer_()) {
    this->disable_preview_after_error_();
    return false;
  }

  memset(this->output_buffer_, 0, PREVIEW_SIZE * PREVIEW_SIZE * 2);

  DecodeContext context;
  context.preview = this;
  context.input = frame->data;
  context.input_len = frame->len;

  JDEC decoder;
  JRESULT result = jd_prepare(&decoder, &UvcDisplayPreview::jpeg_input_, this->work_buffer_, TJPGD_WORK_SIZE, &context);
  if (result != JDR_OK) {
    ESP_LOGW(TAG, "JPEG prepare failed: %d", result);
    return false;
  }

  context.source_width = decoder.width;
  context.source_height = decoder.height;
  context.decode_scale = choose_decode_scale(decoder.width, decoder.height);
  const uint16_t scaled_width = decoder.width >> context.decode_scale;
  const uint16_t scaled_height = decoder.height >> context.decode_scale;
  context.crop_size = std::min<uint16_t>(scaled_width, scaled_height);
  context.crop_x = (scaled_width - context.crop_size) / 2;
  context.crop_y = (scaled_height - context.crop_size) / 2;

  bool expected = false;
  if (this->logged_first_frame_.compare_exchange_strong(expected, true)) {
    ESP_LOGI(TAG, "First preview frame: source=%ux%u scale=1/%u crop=%ux%u+%u+%u", decoder.width, decoder.height,
             1U << context.decode_scale, context.crop_size, context.crop_size, context.crop_x, context.crop_y);
  }

  const uint32_t jpeg_start_ms = millis();
  result = jd_decomp(&decoder, &UvcDisplayPreview::jpeg_output_, context.decode_scale);
  this->last_jpeg_ms_ = millis() - jpeg_start_ms;

  if (result != JDR_OK) {
    ESP_LOGW(TAG, "JPEG decode failed: %d", result);
    return false;
  }

  if (!this->enabled_.load())
    return false;
  return true;
}

bool UvcDisplayPreview::transform_scaled_rgb565_(const uint8_t *scaled_buffer, uint16_t scaled_width,
                                                 uint16_t scaled_height) {
  if (scaled_buffer == nullptr || this->output_buffer_ == nullptr || scaled_width < PREVIEW_SIZE ||
      scaled_height < PREVIEW_SIZE) {
    return false;
  }

  const uint16_t crop_size = std::min<uint16_t>(scaled_width, scaled_height);
  const uint16_t crop_x = (scaled_width - crop_size) / 2;
  const uint16_t crop_y = (scaled_height - crop_size) / 2;

  uint16_t sample_x_offsets[PREVIEW_SIZE];
  size_t sample_row_offsets[PREVIEW_SIZE];
  for (uint16_t i = 0; i < PREVIEW_SIZE; i++) {
    const uint16_t sample = ((static_cast<uint32_t>(i) * crop_size + PREVIEW_SIZE / 2) / PREVIEW_SIZE);
    sample_x_offsets[i] = static_cast<uint16_t>((crop_x + sample) * 2);
    sample_row_offsets[i] = static_cast<size_t>(crop_y + sample) * scaled_width * 2;
  }

  if (crop_size == PREVIEW_SIZE) {
    for (uint16_t src_y = 0; src_y < PREVIEW_SIZE; src_y++) {
      const uint8_t *src_row = scaled_buffer + sample_row_offsets[src_y];
      uint8_t *dest = this->output_buffer_ + static_cast<size_t>(src_y) * 2;
      for (uint16_t src_x = 0; src_x < PREVIEW_SIZE; src_x++) {
        const uint8_t *src = src_row + sample_x_offsets[src_x];
        dest[0] = src[1];
        dest[1] = src[0];
        dest += PREVIEW_SIZE * 2;
      }
    }
    return true;
  }

  for (uint16_t src_y = 0; src_y < PREVIEW_SIZE; src_y++) {
    const uint8_t *src_row = scaled_buffer + sample_row_offsets[src_y];
    uint8_t *dest = this->output_buffer_ + static_cast<size_t>(src_y) * 2;
    for (uint16_t src_x = 0; src_x < PREVIEW_SIZE; src_x++) {
      const uint8_t *src = src_row + sample_x_offsets[src_x];
      dest[0] = src[1];
      dest[1] = src[0];
      dest += PREVIEW_SIZE * 2;
    }
  }

  return true;
}

void UvcDisplayPreview::log_new_jpeg_failure_(const char *reason, int ret, int detail) {
  if (this->logged_new_jpeg_failure_)
    return;
  ESP_LOGW(TAG, "ESP_NEW_JPEG fallback reason: %s ret=%d detail=%d", reason, ret, detail);
  this->logged_new_jpeg_failure_ = true;
}

void UvcDisplayPreview::clear_pending_frame_() {
  if (this->mutex_ == nullptr)
    return;
  if (xSemaphoreTake(this->mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
    this->has_pending_frame_ = false;
    this->decoded_frame_ready_ = false;
    this->ready_buffer_index_ = -1;
    xSemaphoreGive(this->mutex_);
  }
}

void UvcDisplayPreview::set_lvgl_paused_(bool paused) {
  if (this->lvgl_ == nullptr || this->lvgl_paused_ == paused)
    return;
  this->lvgl_->set_paused(paused, false);
  this->lvgl_paused_ = paused;
}

void UvcDisplayPreview::disable_preview_after_error_() {
  ESP_LOGE(TAG, "Preview disabled after internal error");
  this->preview_failed_.store(true);
  this->enabled_.store(false);
  this->clear_pending_frame_();
}

UINT UvcDisplayPreview::jpeg_input_(JDEC *decoder, BYTE *buffer, UINT len) {
  auto *context = static_cast<DecodeContext *>(decoder->device);
  if (context == nullptr || context->input == nullptr)
    return 0;

  const size_t remaining = context->input_len - std::min(context->input_pos, context->input_len);
  const size_t to_read = std::min<size_t>(len, remaining);
  if (buffer != nullptr && to_read > 0)
    memcpy(buffer, context->input + context->input_pos, to_read);
  context->input_pos += to_read;
  return to_read;
}

UINT UvcDisplayPreview::jpeg_output_(JDEC *decoder, void *bitmap, JRECT *rect) {
  auto *context = static_cast<DecodeContext *>(decoder->device);
  if (context == nullptr || context->preview == nullptr || bitmap == nullptr || rect == nullptr)
    return 0;

  auto *area = rect;
  auto *pixels = static_cast<uint8_t *>(bitmap);
  auto *output = context->preview->output_buffer_;
  if (output == nullptr || !context->preview->enabled_.load())
    return 0;

  const uint16_t crop_right = context->crop_x + context->crop_size;
  const uint16_t crop_bottom = context->crop_y + context->crop_size;
  if (area->right < context->crop_x || area->left >= crop_right || area->bottom < context->crop_y || area->top >= crop_bottom)
    return 1;

  const uint16_t block_width = area->right - area->left + 1;
  const uint16_t block_height = area->bottom - area->top + 1;

  for (uint16_t y = 0; y < block_height; y++) {
    const uint16_t src_y = area->top + y;
    if (src_y < context->crop_y || src_y >= crop_bottom)
      continue;
    const uint16_t dest_y = ((src_y - context->crop_y) * PREVIEW_SIZE) / context->crop_size;
    if (dest_y >= PREVIEW_SIZE)
      continue;

    for (uint16_t x = 0; x < block_width; x++) {
      const uint16_t src_x = area->left + x;
      if (src_x < context->crop_x || src_x >= crop_right)
        continue;
      const uint16_t dest_x = ((src_x - context->crop_x) * PREVIEW_SIZE) / context->crop_size;
      if (dest_x >= PREVIEW_SIZE)
        continue;

      const size_t source_index = (static_cast<size_t>(y) * block_width + x) * 3;
      const uint8_t red = pixels[source_index + 0];
      const uint8_t green = pixels[source_index + 1];
      const uint8_t blue = pixels[source_index + 2];
      const uint16_t color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
      const uint16_t rotated_x = dest_y;
      const uint16_t rotated_y = dest_x;
      const size_t dest_index = (static_cast<size_t>(rotated_y) * PREVIEW_SIZE + rotated_x) * 2;
      output[dest_index] = color >> 8;
      output[dest_index + 1] = color & 0xFF;
    }
  }

  return 1;
}

}  // namespace uvc_display_preview
}  // namespace esphome

#endif  // USE_ESP32
