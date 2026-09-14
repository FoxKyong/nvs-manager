#include "NvsBootGuard.h"

#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

extern "C" {
esp_err_t __real_nvs_flash_init(void);
esp_err_t __real_esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size);
esp_err_t __wrap_nvs_flash_init(void);
esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size);
}

namespace {

// Written from the startup path before the scheduler hands control to
// setup(), read afterwards; no concurrent access.
bool g_initSeen = false;
esp_err_t g_initResult = ESP_OK;
uint32_t g_blockedErases = 0;

bool isEraseTrigger(esp_err_t err) {
    return err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND;
}

} // namespace

esp_err_t __wrap_nvs_flash_init(void) {
    esp_err_t err = __real_nvs_flash_init();
    if (!g_initSeen) {
        g_initSeen = true;
        g_initResult = err;
    }
    // Hide the two codes that make the caller erase the partition. The real
    // result stays available through bootReport().
    return isEraseTrigger(err) ? ESP_FAIL : err;
}

esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size) {
    // NVS garbage collection erases single sectors and must keep working;
    // only an erase of the whole NVS partition is refused.
    const bool wholeNvs = partition != nullptr && partition->type == ESP_PARTITION_TYPE_DATA &&
                          partition->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS && offset == 0 &&
                          size >= partition->size;
    if (wholeNvs) {
        ++g_blockedErases;
        return ESP_ERR_NOT_ALLOWED;
    }
    return __real_esp_partition_erase_range(partition, offset, size);
}

namespace nvsguard {

BootReport bootReport() { return {g_initSeen, g_initResult, g_blockedErases}; }

} // namespace nvsguard
