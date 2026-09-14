// SPDX-License-Identifier: MIT
//
// nvs_flash declares lookup_nvs_encrypted_partition for every target but
// defines it only outside LINUX_TARGET, where the encrypted-partition code is
// built. The desktop build has no NVS encryption, as ESP-IDF's own Linux
// target does not; this definition only lets nvs_flash link and reports that.

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace nvs {

class NVSPartition;

namespace partition_lookup {

esp_err_t lookup_nvs_encrypted_partition(const char *, nvs_sec_cfg_t *, NVSPartition **) {
    return ESP_ERR_NVS_ENCR_NOT_SUPPORTED;
}

} // namespace partition_lookup

} // namespace nvs
