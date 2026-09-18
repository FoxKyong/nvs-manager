#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/Export.h"
#include "app/NvsModel.h"

// Restoring a raw backup (app/Export.h) over the whole NVS partition.
namespace app {

// One raw backup on the SD card, as its manifest describes it.
struct BackupInfo {
    std::string name;          // "nvs-0003"
    std::string binPath;       // "/NVSManager/backups/nvs-0003.bin"
    bool hasManifest = false;
    std::string manifestError; // why the manifest cannot be used, when !hasManifest
    std::string toolVersion;
    std::string device;
    std::string deviceId;      // empty in manifests written before 1.0.0
    std::string label;
    uint64_t address = 0;
    uint64_t size = 0;
    uint64_t bytes = 0;
    std::string sha256;
    bool encrypted = false;
    bool haveStats = false;
    nvsm::Stats stats;         // at the time of the backup
};

// Raw backups in /NVSManager/backups, newest (highest number) first. False
// only when the card cannot be used; no backups is an empty list.
bool listBackups(std::vector<BackupInfo> &out, std::string &error);

struct RestoreCheck {
    bool ok = false;                   // every check passed
    std::vector<std::string> problems; // each one blocks the restore
    std::vector<std::string> warnings; // shown, but do not block
    std::vector<uint8_t> image;        // the checked backup, when ok
};

// Reads the backup and checks it against its manifest, this partition and
// this device. Reads only.
RestoreCheck checkBackup(const NvsModel &model, const BackupInfo &backup);

struct RestoreResult {
    ExportResult safety;    // raw backup of the NVS as it was just before
    bool attempted = false; // false when that backup failed and nothing was written
    ChangeOutcome change;
};

// Saves the current NVS as a new raw backup, then writes the checked image
// over the whole partition. Only call it with a check that is ok.
RestoreResult restoreBackup(NvsModel &model, const BackupInfo &backup, const RestoreCheck &check);

} // namespace app
