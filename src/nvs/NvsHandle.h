#pragma once

#include <string>

#include <esp_err.h>
#include <nvs.h>

namespace nvsm {

// An NVS handle that is always closed, so no early return leaks one.
class ScopedHandle {
public:
    ScopedHandle(const std::string &label, const std::string &ns, nvs_open_mode_t mode) {
        err_ = nvs_open_from_partition(label.c_str(), ns.c_str(), mode, &handle_);
    }
    ~ScopedHandle() {
        if (err_ == ESP_OK) nvs_close(handle_);
    }
    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;

    esp_err_t error() const { return err_; }
    nvs_handle_t get() const { return handle_; }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t err_ = ESP_FAIL;
};

} // namespace nvsm
