#include "ui/DashboardScreen.h"

#include <cstdio>

#include <nvs.h>

#include "platform/Platform.h"
#include "ui/Actions.h"
#include "ui/BackupScreen.h"
#include "ui/Draw.h"
#include "ui/HelpScreen.h"
#include "ui/NamespaceListScreen.h"
#include "ui/UiController.h"

namespace ui {
namespace {

// A short hint per error state. Every one of them is shown without erasing
// anything; the tool never "repairs" NVS by itself.
const char *errorHint(esp_err_t err) {
    switch (err) {
    case ESP_ERR_NVS_NO_FREE_PAGES: return "No free page left. Nothing was erased.";
    case ESP_ERR_NVS_NEW_VERSION_FOUND: return "Written by a newer ESP-IDF. Nothing was erased.";
    case ESP_ERR_NOT_FOUND:
    case ESP_ERR_NVS_PART_NOT_FOUND: return "No NVS partition with this label.";
    default: return "Nothing was erased.";
    }
}

} // namespace

void DashboardScreen::draw(lgfx::LovyanGFX &g) {
    const app::NvsModel &m = ui_.model();
    const int w = g.width();
    char buf[64];

    const bool sd = platform::storageMounted();
    const char *sdLabel = sd ? "SD" : "no SD";
    g.setFont(theme::fontSmall());
    const int sdBadgeW = g.textWidth(sdLabel) + 6;
    drawHeader(g, "NVS Manager", nullptr, sdBadgeW + 4);
    drawBadge(g, w - 3, 2, theme::kHeaderH - 4, sdLabel, sd ? theme::kHealthOk : theme::kFooter, true);
    if (m.error() != ESP_OK) {
        drawError(g);
        drawFooter(g, "R:retry  I:info");
        return;
    }

    const app::Capacity cap = app::capacityOf(m.stats());
    const nvsm::Stats &st = m.stats();

    int y = theme::kHeaderH + 4;
    drawBadge(g, 4, y, 14, app::healthName(cap.health), healthColor(cap.health), false);
    g.setFont(theme::fontSmall());
    g.setTextColor(theme::kTextDim);
    g.setTextDatum(textdatum_t::middle_right);
    if (m.hasPartition()) {
        snprintf(buf, sizeof buf, "%s  %s @0x%X", m.label().c_str(), formatBytes(m.partition().size).c_str(),
                 static_cast<unsigned>(m.partition().address));
        g.drawString(buf, w - 4, y + 8);
    }

    y = theme::kHeaderH + 22;
    if (cap.health == app::Health::Full) {
        g.setFont(theme::fontNormal());
        g.setTextDatum(textdatum_t::top_left);
        g.setTextColor(theme::kHealthFull);
        g.drawString("NVS FULL", 4, y);
        // Two lines only: the usage bar starts right below.
        g.setFont(theme::fontSmall());
        g.setTextColor(theme::kText);
        g.drawString("No entries available for new data.", 4, y + 15);
        g.drawString("Settings may silently fail to save.", 4, y + 26);
    } else {
        g.setFont(theme::fontSmall());
        g.setTextColor(theme::kTextDim);
        g.setTextDatum(textdatum_t::top_left);
        g.drawString("Available", 4, y);

        g.setFont(theme::fontLarge());
        g.setTextColor(theme::kText);
        snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(cap.available));
        g.drawString(buf, 4, y + 10);
        const int numberWidth = g.textWidth(buf);

        g.setFont(theme::fontNormal());
        g.setTextColor(theme::kTextDim);
        g.setTextDatum(textdatum_t::bottom_left);
        snprintf(buf, sizeof buf, "of %u usable  %d%%", static_cast<unsigned>(cap.usable), cap.percent);
        g.drawString(buf, 4 + numberWidth + 6, y + 31);
    }

    // Used share of the usable entries.
    const int barY = theme::kHeaderH + 66;
    const int barW = w - 8;
    g.fillRect(4, barY, barW, 7, theme::kBarTrack);
    if (cap.usable > 0) {
        const int usedW = static_cast<int>((cap.usable - cap.available) * static_cast<size_t>(barW) / cap.usable);
        g.fillRect(4, barY, usedW, 7, healthColor(cap.health));
    }

    g.setFont(theme::fontSmall());
    g.setTextDatum(textdatum_t::top_left);
    g.setTextColor(theme::kTextDim);
    snprintf(buf, sizeof buf, "Used %u   Free %u   Namespaces %u", static_cast<unsigned>(st.usedEntries),
             static_cast<unsigned>(st.freeEntries), static_cast<unsigned>(st.namespaceCount));
    g.drawString(clipText(g, buf, w - 8).c_str(), 4, barY + 11);
    if (m.unattributedEntries() > 0) {
        snprintf(buf, sizeof buf, "%u in empty or orphaned namespaces",
                 static_cast<unsigned>(m.unattributedEntries()));
        g.drawString(clipText(g, buf, w - 8).c_str(), 4, barY + 22);
    }

    drawFooter(g, ui_.advanced() ? "Enter:list  B:backup  A:lock  I:info" : "Enter:list  B:backup  A:adv  I:info");
}

void DashboardScreen::drawError(lgfx::LovyanGFX &g) {
    const app::NvsModel &m = ui_.model();
    const int w = g.width();
    int y = theme::kHeaderH + 4;
    drawBadge(g, 4, y, 14, "ERROR", theme::kHealthFull, false);

    y += 20;
    g.setTextDatum(textdatum_t::top_left);
    g.setFont(theme::fontNormal());
    g.setTextColor(theme::kText);
    g.drawString("NVS cannot be read", 4, y);

    g.setFont(theme::fontSmall());
    g.setTextColor(theme::kHealthFull);
    char buf[64];
    snprintf(buf, sizeof buf, "%s (0x%x)", esp_err_to_name(m.error()), static_cast<unsigned>(m.error()));
    g.drawString(clipText(g, buf, w - 8).c_str(), 4, y + 17);
    g.setTextColor(theme::kTextDim);
    snprintf(buf, sizeof buf, "during: %s", m.errorStep());
    g.drawString(clipText(g, buf, w - 8).c_str(), 4, y + 29);
    g.setTextColor(theme::kText);
    g.drawString(clipText(g, errorHint(m.error()), w - 8).c_str(), 4, y + 44);
}

void DashboardScreen::handle(const Input &in) {
    if (in.nav == Nav::Enter && ui_.model().error() == ESP_OK) {
        ui_.push(std::unique_ptr<Screen>(new NamespaceListScreen(ui_)));
    } else if (in.nav == Nav::Back) {
        confirmExit(ui_);
    } else if (in.nav == Nav::Char) {
        switch (in.ch) {
        case 'r':
        case 'R': ui_.refreshModel(); break;
        case 'a':
        case 'A': toggleAdvancedMode(ui_); break;
        case 'b':
        case 'B': ui_.push(std::unique_ptr<Screen>(new BackupScreen(ui_))); break;
        case 'i':
        case 'I': ui_.push(std::unique_ptr<Screen>(new HelpScreen(ui_))); break;
        default: break;
        }
    }
}

} // namespace ui
