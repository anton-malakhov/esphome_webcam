// SPDX-License-Identifier: GPL-3.0-only
// This file is derrived from original esp32_camera component
// from ESPHome with modifications for usb_webcam component

#pragma once

#ifdef USE_ESP32

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/camera/camera.h"
#include "esphome/core/helpers.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace esphome::usb_webcam {
using namespace esphome::camera;
/* ---------------- enum classes ---------------- */

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

enum USBWebCamFrameSize {
  USB_WEBCAM_SIZE_160X120,    // QQVGA
  USB_WEBCAM_SIZE_176X144,    // QCIF
  USB_WEBCAM_SIZE_240X176,    // HQVGA
  USB_WEBCAM_SIZE_320X240,    // QVGA
  USB_WEBCAM_SIZE_400X296,    // CIF
  USB_WEBCAM_SIZE_640X480,    // VGA
  USB_WEBCAM_SIZE_800X600,    // SVGA
  USB_WEBCAM_SIZE_1024X768,   // XGA
  USB_WEBCAM_SIZE_1280X1024,  // SXGA
  USB_WEBCAM_SIZE_1600X1200,  // UXGA
  USB_WEBCAM_SIZE_1920X1080,  // FHD
  USB_WEBCAM_SIZE_720X1280,   // PHD
  USB_WEBCAM_SIZE_864X1536,   // P3MP
  USB_WEBCAM_SIZE_2048X1536,  // QXGA
  USB_WEBCAM_SIZE_2560X1440,  // QHD
  USB_WEBCAM_SIZE_2560X1600,  // WQXGA
  USB_WEBCAM_SIZE_1080X1920,  // PFHD
  USB_WEBCAM_SIZE_2560X1920,  // QSXGA
};

/* ---------------- CameraImage class ---------------- */
typedef struct {
    uint8_t * buf;              // Pointer to the pixel data
    size_t len;                 // Length of the buffer in bytes
    size_t width;               // Width of the buffer in pixels
    size_t height;              // Height of the buffer in pixels
    pixformat_t format;         // Format of the pixel data
    struct timeval timestamp;   // Timestamp since boot of the first DMA buffer of the frame
} camera_fb_t;

class USBWebCam;

class USBWebCamImage : public camera::CameraImage {
 public:
  USBWebCamImage(camera_fb_t *buffer, uint8_t requester);
  camera_fb_t *get_raw_buffer();
  uint8_t *get_data_buffer() override;
  size_t get_data_length() override;
  bool was_requested_by(camera::CameraRequester requester) const override;

 protected:
  camera_fb_t *buffer_;
  uint8_t requesters_;
};

struct CameraImageData {
  uint8_t *data;
  size_t length;
};

/* ---------------- CameraImageReader class ---------------- */
class USBWebCamImageReader : public camera::CameraImageReader {
 public:
  USBWebCamImageReader() {}
  ~USBWebCamImageReader() {}
  void set_image(std::shared_ptr<camera::CameraImage> image) override;
  size_t available() const override;
  uint8_t *peek_data_buffer() override;
  void consume_data(size_t consumed) override;
  void return_image() override;

 protected:
  std::shared_ptr<USBWebCamImage> image_;
  size_t offset_{0};
};

/* ---------------- USBWebCam class ---------------- */
class USBWebCam : public camera::Camera {
 public:
  USBWebCam();

  /* setters */
  /* -- image */
  void set_frame_size(USBWebCamFrameSize size);
  void set_drop_size(uint32_t drop_size);
  /* -- framerates */
  void set_max_update_interval(uint32_t max_update_interval);
  void set_idle_update_interval(uint32_t idle_update_interval);

  /* public API (derivated) */
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  /* public API (specific) */
  void start_stream(camera::CameraRequester requester) override;
  void stop_stream(camera::CameraRequester requester) override;
  void request_image(camera::CameraRequester requester) override;
  void update_camera_parameters();

  void add_image_callback(std::function<void(std::shared_ptr<camera::CameraImage>)> &&callback) override;
  void add_stream_start_callback(std::function<void()> &&callback);
  void add_stream_stop_callback(std::function<void()> &&callback);
  camera::CameraImageReader *create_image_reader() override;

 protected:
  /* internal methods */
  bool has_requested_image_() const;
  bool can_return_image_() const;

  static void framebuffer_task(void *pv);

  /* attributes */
  /* camera configuration */
  USBWebCamFrameSize frame_size;
  /* -- framerates */
  uint32_t max_update_interval_{1000};
  uint32_t idle_update_interval_{15000};

  esp_err_t init_error_{ESP_OK};
  std::shared_ptr<USBWebCamImage> current_image_;
  uint8_t single_requesters_{0};
  uint8_t stream_requesters_{0};
  QueueHandle_t framebuffer_get_queue_;
  QueueHandle_t framebuffer_return_queue_;
  CallbackManager<void(std::shared_ptr<camera::CameraImage>)> new_image_callback_{};
  CallbackManager<void()> stream_start_callback_{};
  CallbackManager<void()> stream_stop_callback_{};

  uint64_t last_idle_request_{0};
  uint64_t last_update_{0};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern USBWebCam *global_usb_webcam;

class USBWebCamStreamStartTrigger : public Trigger<> {
 public:
  explicit USBWebCamStreamStartTrigger(USBWebCam *parent) {
    parent->add_stream_start_callback([this]() { this->trigger(); });
  }

 protected:
};
class USBWebCamStreamStopTrigger : public Trigger<> {
 public:
  explicit USBWebCamStreamStopTrigger(USBWebCam *parent) {
    parent->add_stream_stop_callback([this]() { this->trigger(); });
  }

 protected:
};

}  // namespace esphome::usb_webcam

#endif
