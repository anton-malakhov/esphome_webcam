// SPDX-License-Identifier: GPL-3.0-only
// This file is derrived from original esp32_camera component from ESPHome
// with modifications for usb_webcam component

#pragma once

#ifdef USE_ESP32

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#if 1
typedef enum {
    PIXFORMAT_RGB565,    // 2BPP/RGB565
    PIXFORMAT_YUV422,    // 2BPP/YUV422
    PIXFORMAT_GRAYSCALE, // 1BPP/GRAYSCALE
    PIXFORMAT_JPEG,      // JPEG/COMPRESSED
    PIXFORMAT_RGB888,    // 3BPP/RGB888
    PIXFORMAT_RAW,       // RAW
    PIXFORMAT_RGB444,    // 3BP2P/RGB444
    PIXFORMAT_RGB555,    // 3BP2P/RGB555
} pixformat_t;

typedef struct {
    uint8_t * buf;              // Pointer to the pixel data
    size_t len;                 // Length of the buffer in bytes
    size_t width;               // Width of the buffer in pixels
    size_t height;              // Height of the buffer in pixels
    pixformat_t format;         // Format of the pixel data
    struct timeval timestamp;   // Timestamp since boot of the first DMA buffer of the frame
} camera_fb_t;
#else
// these definitions are taken from
#include "esp_camera.h"
#endif

namespace esphome {
namespace esp32_camera {
class dummy : public Component {
 public:
  void setup() override { }
  float get_setup_priority() const override { return setup_priority::BUS; }
};

class ESP32Camera;

/* ---------------- enum classes ---------------- */
enum CameraRequester { IDLE, API_REQUESTER, WEB_REQUESTER };

enum ESP32CameraFrameSize {
  ESP32_CAMERA_SIZE_160X120,    // QQVGA
  ESP32_CAMERA_SIZE_176X144,    // QCIF
  ESP32_CAMERA_SIZE_240X176,    // HQVGA
  ESP32_CAMERA_SIZE_320X240,    // QVGA
  ESP32_CAMERA_SIZE_400X296,    // CIF
  ESP32_CAMERA_SIZE_640X480,    // VGA
  ESP32_CAMERA_SIZE_800X600,    // SVGA
  ESP32_CAMERA_SIZE_1024X768,   // XGA
  ESP32_CAMERA_SIZE_1280X1024,  // SXGA
  ESP32_CAMERA_SIZE_1600X1200,  // UXGA
  ESP32_CAMERA_SIZE_1920X1080,  // FHD
  ESP32_CAMERA_SIZE_720X1280,   // PHD
  ESP32_CAMERA_SIZE_864X1536,   // P3MP
  ESP32_CAMERA_SIZE_2048X1536,  // QXGA
  ESP32_CAMERA_SIZE_2560X1440,  // QHD
  ESP32_CAMERA_SIZE_2560X1600,  // WQXGA
  ESP32_CAMERA_SIZE_1080X1920,  // PFHD
  ESP32_CAMERA_SIZE_2560X1920,  // QSXGA
};

/* ---------------- CameraImage class ---------------- */
class CameraImage {
 public:
  CameraImage(camera_fb_t *buffer, uint8_t requester);
  camera_fb_t *get_raw_buffer();
  uint8_t *get_data_buffer();
  size_t get_data_length();
  bool was_requested_by(CameraRequester requester) const;

 protected:
  camera_fb_t *buffer_;
  uint8_t requesters_;
};

/* ---------------- CameraImageReader class ---------------- */
class CameraImageReader {
 public:
  void set_image(std::shared_ptr<CameraImage> image);
  size_t available() const;
  uint8_t *peek_data_buffer();
  void consume_data(size_t consumed);
  void return_image();

 protected:
  std::shared_ptr<CameraImage> image_;
  size_t offset_{0};
};

/* ---------------- ESP32Camera class ---------------- */
class ESP32Camera : public Component, public EntityBase {
 public:
  ESP32Camera();

  /* setters */
  /* -- image */
  void set_frame_size(ESP32CameraFrameSize size);
  /* -- framerates */
  void set_max_update_interval(uint32_t max_update_interval);
  void set_idle_update_interval(uint32_t idle_update_interval);

  /* public API (derivated) */
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  /* public API (specific) */
  void add_image_callback(std::function<void(std::shared_ptr<CameraImage>)> &&f);
  void start_stream(CameraRequester requester);
  void stop_stream(CameraRequester requester);
  void request_image(CameraRequester requester);
  void update_camera_parameters();

  void add_stream_start_callback(std::function<void()> &&callback);
  void add_stream_stop_callback(std::function<void()> &&callback);

 protected:
  /* internal methods */
  bool has_requested_image_() const;
  bool can_return_image_() const;

  static void framebuffer_task(void *pv);

  /* attributes */
  /* camera configuration */
  ESP32CameraFrameSize frame_size;
  /* -- framerates */
  uint32_t max_update_interval_{1000};
  uint32_t idle_update_interval_{15000};

  esp_err_t init_error_{ESP_OK};
  std::shared_ptr<CameraImage> current_image_;
  uint8_t single_requesters_{0};
  uint8_t stream_requesters_{0};
  QueueHandle_t framebuffer_get_queue_;
  QueueHandle_t framebuffer_return_queue_;
  CallbackManager<void(std::shared_ptr<CameraImage>)> new_image_callback_;
  CallbackManager<void()> stream_start_callback_{};
  CallbackManager<void()> stream_stop_callback_{};

  uint64_t last_idle_request_{0};
  uint64_t last_update_{0};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern ESP32Camera *global_esp32_camera;

class ESP32CameraStreamStartTrigger : public Trigger<> {
 public:
  explicit ESP32CameraStreamStartTrigger(ESP32Camera *parent) {
    parent->add_stream_start_callback([this]() { this->trigger(); });
  }

 protected:
};
class ESP32CameraStreamStopTrigger : public Trigger<> {
 public:
  explicit ESP32CameraStreamStopTrigger(ESP32Camera *parent) {
    parent->add_stream_stop_callback([this]() { this->trigger(); });
  }

 protected:
};

}  // namespace esp32_camera
}  // namespace esphome

#endif
