#include "ui/UiController.h"

#include "app/Config.h"
#include "ui/DashboardScreen.h"
#include "ui/Draw.h"
#include "ui/Theme.h"

namespace ui {

UiController::UiController(app::NvsModel &model) : model_(model), canvas_(&platform::display()) {}

void UiController::begin() {
    auto &display = platform::display();
    width_ = display.width();
    height_ = display.height();

    canvas_.setColorDepth(16);
    haveCanvas_ = canvas_.createSprite(width_, height_) != nullptr;
    if (!haveCanvas_) platform::logf("ERR", "no memory for a %dx%d canvas, drawing directly", width_, height_);

    stack_.emplace_back(new DashboardScreen(*this));
    lastInputMs_ = platform::millis();
    dirty_ = true;
}

void UiController::handleKey(const platform::KeyEvent &ev) {
    lastInputMs_ = platform::millis();
    if (stack_.empty()) return;
    Screen &top = *stack_.back();
    top.handle(translate(ev, top.textMode()));
    applyPending();
}

void UiController::update() {
    if (advanced_ && platform::millis() - lastInputMs_ >= app::config::kAdvancedIdleLockMs) {
        setAdvanced(false);
    }
    if (!stack_.empty()) stack_.back()->tick();
    applyPending();
}

void UiController::render() {
    if (!dirty_ || stack_.empty()) return;
    dirty_ = false;

    lgfx::LovyanGFX &target = haveCanvas_ ? static_cast<lgfx::LovyanGFX &>(canvas_) : platform::display();
    target.startWrite();
    target.fillScreen(theme::kBackground);
    stack_.back()->draw(target);
    if (advanced_) {
        drawBadge(target, width_ - 2, height_ - theme::kFooterH + 1, theme::kFooterH - 2, "ADV", theme::kHealthFull, true);
    }
    target.endWrite();
    if (haveCanvas_) canvas_.pushSprite(0, 0);
}

void UiController::push(std::unique_ptr<Screen> screen) {
    pendingPush_.push_back(std::move(screen));
    dirty_ = true;
}

void UiController::pop() {
    ++pendingPops_;
    dirty_ = true;
}

void UiController::refreshModel() {
    model_.refresh();
    platform::logf("NVS", "refreshed: used=%u available=%u namespaces=%u", static_cast<unsigned>(model_.stats().usedEntries),
                   static_cast<unsigned>(model_.stats().availableEntries),
                   static_cast<unsigned>(model_.namespaces().size()));
    modelChanged();
}

void UiController::modelChanged() {
    for (auto &screen : stack_) screen->onModelRefreshed();
    dirty_ = true;
}

void UiController::setAdvanced(bool on) {
    if (advanced_ == on) return;
    advanced_ = on;
    platform::logf("UI", "advanced mode %s", on ? "unlocked" : "locked");
    dirty_ = true;
}

void UiController::applyPending() {
    // The dashboard at the bottom is never popped.
    for (; pendingPops_ > 0 && stack_.size() > 1; --pendingPops_) stack_.pop_back();
    pendingPops_ = 0;
    for (auto &screen : pendingPush_) stack_.push_back(std::move(screen));
    pendingPush_.clear();
}

} // namespace ui
