#include "app/Restore.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>

#include "nvs/NvsImage.h"
#include "platform/Platform.h"
#include "util/JsonReader.h"
#include "util/Sha256.h"

namespace app {
namespace {

constexpr const char *kBackupDir = "/NVSManager/backups";

// "nvs-NNNN.bin", as rawBackup() names them.
bool isBackupFile(const std::string &name) {
    return name.size() == 12 && name.compare(0, 4, "nvs-") == 0 && name.compare(8, 4, ".bin") == 0 &&
           std::all_of(name.begin() + 4, name.begin() + 8, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

void readManifest(BackupInfo &info) {
    const std::string path = std::string(kBackupDir) + "/" + info.name + ".json";
    std::vector<uint8_t> raw;
    if (!platform::storageRead(path, raw)) {
        info.manifestError = "No manifest " + info.name + ".json";
        return;
    }
    util::JsonValue root;
    std::string error;
    if (!util::parseJson(std::string(raw.begin(), raw.end()), root, error)) {
        info.manifestError = "Manifest cannot be read (" + error + ")";
        return;
    }
    if (root.str("tool") != "NVS Manager") {
        info.manifestError = "Manifest was not written by NVS Manager";
        return;
    }
    info.hasManifest = true;
    info.toolVersion = root.str("version");
    info.device = root.str("device");
    info.deviceId = root.str("device_id");
    info.bytes = root.u64("bytes");
    info.sha256 = root.str("sha256");
    info.encrypted = root.flag("encrypted");
    if (const util::JsonValue *p = root.get("partition")) {
        info.label = p->str("label");
        info.address = p->u64("address");
        info.size = p->u64("size");
    }
    if (const util::JsonValue *s = root.get("stats")) {
        info.haveStats = true;
        info.stats.usedEntries = s->u64("used");
        info.stats.freeEntries = s->u64("free");
        info.stats.availableEntries = s->u64("available");
        info.stats.totalEntries = s->u64("total");
        info.stats.namespaceCount = s->u64("namespaces");
    }
}

} // namespace

bool listBackups(std::vector<BackupInfo> &out, std::string &error) {
    out.clear();
    if (!platform::storageMounted()) {
        error = platform::storageProblem();
        return false;
    }
    std::vector<std::string> names;
    if (!platform::storageList(kBackupDir, names)) return true; // no backup made yet
    std::sort(names.begin(), names.end(), std::greater<std::string>()); // fixed-width numbers
    for (const auto &name : names) {
        if (!isBackupFile(name)) continue;
        BackupInfo info;
        info.name = name.substr(0, 8);
        info.binPath = std::string(kBackupDir) + "/" + name;
        readManifest(info);
        out.push_back(std::move(info));
    }
    return true;
}

RestoreCheck checkBackup(const NvsModel &model, const BackupInfo &b) {
    RestoreCheck c;
    char buf[160];
    if (!model.hasPartition()) {
        c.problems.push_back("This device has no NVS partition labelled " + model.label() + ".");
        return c;
    }
    const nvsm::PartitionInfo &part = model.partition();
    if (!b.hasManifest) {
        c.problems.push_back(b.manifestError + ". Without it the backup cannot be checked.");
        return c;
    }
    if (part.encrypted || b.encrypted) c.problems.push_back("Encrypted NVS cannot be restored.");
    if (b.label != part.label || b.address != part.address || b.size != part.size) {
        snprintf(buf, sizeof buf, "Made for partition %s at 0x%X, %u bytes; this one is %s at 0x%X, %u bytes.",
                 b.label.c_str(), static_cast<unsigned>(b.address), static_cast<unsigned>(b.size), part.label.c_str(),
                 static_cast<unsigned>(part.address), static_cast<unsigned>(part.size));
        c.problems.push_back(buf);
    }
    if (b.deviceId.empty()) {
        c.warnings.push_back("Made by NVS Manager " + (b.toolVersion.empty() ? std::string("?") : b.toolVersion) +
                             ", which did not record the device. Restore it only if it comes from this device: "
                             "NVS holds its radio calibration.");
    } else if (b.deviceId != platform::deviceId()) {
        c.problems.push_back("Made on another device. NVS holds per-device radio calibration and pairings, "
                             "so only a backup of this device can be restored.");
    }
    if (!c.problems.empty()) return c;

    if (!platform::storageRead(b.binPath, c.image)) {
        c.problems.push_back("Cannot read " + b.binPath + ".");
        return c;
    }
    const auto digest = util::sha256(c.image.data(), c.image.size());
    if (c.image.size() != b.bytes || util::toHex(digest.data(), digest.size()) != b.sha256) {
        c.problems.push_back("The file does not match the size and SHA-256 in its manifest: it is damaged or was changed.");
    } else {
        const nvsm::ImageCheck img = nvsm::checkImage(c.image, part.size);
        if (!img.ok) {
            c.problems.push_back("Not a valid NVS image: " + img.problem + ".");
        } else {
            if (img.usedPages == 0) {
                c.warnings.push_back("The backup is an empty NVS: restoring it removes every setting.");
            }
            if (img.corruptPages > 0) {
                snprintf(buf, sizeof buf, "%u page(s) of the backup are marked corrupt; NVS ignores them.",
                         static_cast<unsigned>(img.corruptPages));
                c.warnings.push_back(buf);
            }
        }
    }
    if (c.problems.empty()) {
        nvsm::PartitionInfo info;
        std::vector<uint8_t> now;
        if (model.readPartition(info, now) == ESP_OK && now == c.image) {
            c.problems.push_back("NVS already matches this backup byte for byte. Nothing to restore.");
        }
    }

    c.ok = c.problems.empty();
    if (!c.ok) c.image.clear();
    return c;
}

RestoreResult restoreBackup(NvsModel &model, const BackupInfo &backup, const RestoreCheck &check) {
    RestoreResult r;
    r.safety = rawBackup(model);
    if (!r.safety.ok) return r;
    r.attempted = true;
    r.change = model.restorePartition(check.image, backup.name);
    return r;
}

} // namespace app
