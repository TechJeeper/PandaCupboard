#include "ui/App.h"

#include "pt/pt_display.h"
#include "ui/Keyboard.h"
#include "ui/Notify.h"
#include "ui/Theme.h"

#include <WiFi.h>
#include <cstring>
#include <vector>

namespace {
constexpr int kColName = 250;
constexpr int kColTask = 270;

void styleTa(lv_obj_t *ta) {
    paxx_set_form_width(ta);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_height(ta, 42);
}

enum class StatusTone { Normal, Progress, Pause, Stop, Error };

void fillStatus(const PrinterLive &live, const PrinterProfile &profile, char *out, size_t outLen,
                int *percentOut, StatusTone *toneOut = nullptr) {
    *percentOut = -1;
    if (toneOut) *toneOut = StatusTone::Normal;
    if (!profile.ip[0]) {
        snprintf(out, outLen, "Not configured");
        return;
    }
    if (printerIsBambu(profile) && !profile.accessCode[0]) {
        snprintf(out, outLen, "Missing access code");
        return;
    }
    if (!live.online && live.lastSeenMs == 0) {
        if (live.state == BambuGcodeState::Syncing) snprintf(out, outLen, "syncing");
        else snprintf(out, outLen, "%s", live.lastError[0] ? live.lastError : "Offline");
        return;
    }
    if (!live.online && live.state == BambuGcodeState::Offline) {
        snprintf(out, outLen, "%s", live.lastError[0] ? live.lastError : "Offline");
        return;
    }
    if (live.state == BambuGcodeState::Failed || (live.printError != 0 && live.state != BambuGcodeState::Pause)) {
        if (toneOut) *toneOut = StatusTone::Error;
        snprintf(out, outLen, "Failed");
        return;
    }
    if (live.state == BambuGcodeState::Pause) {
        if (toneOut) *toneOut = StatusTone::Pause;
        if (live.percent >= 0) snprintf(out, outLen, "Paused  %d%%", live.percent);
        else snprintf(out, outLen, "Paused");
        return;
    }
    if (live.state == BambuGcodeState::Stopped) {
        if (toneOut) *toneOut = StatusTone::Stop;
        snprintf(out, outLen, "Stopped");
        return;
    }
    if (bambuStateShowsProgress(live.state) && live.percent >= 0) {
        if (toneOut) *toneOut = StatusTone::Progress;
        char remain[24] = {};
        bambuFormatRemain(live.remainingMin, remain, sizeof(remain));
        if (remain[0]) snprintf(out, outLen, "%d%% | %s", live.percent, remain);
        else snprintf(out, outLen, "%d%%", live.percent);
        *percentOut = live.percent;
        return;
    }
    snprintf(out, outLen, "%s", bambuGcodeStateName(live.state));
}

lv_color_t statusToneColor(StatusTone tone) {
    switch (tone) {
        case StatusTone::Progress: return PaxxTheme::accent();
        case StatusTone::Pause:
        case StatusTone::Stop: return PaxxTheme::warn();
        case StatusTone::Error: return PaxxTheme::danger();
        default: return PaxxTheme::text();
    }
}

void formatTimeout(uint16_t sec, char *buf, size_t n) {
    if (sec == 0) {
        snprintf(buf, n, "Never");
        return;
    }
    const unsigned h = static_cast<unsigned>(sec) / 3600;
    const unsigned m = (static_cast<unsigned>(sec) % 3600) / 60;
    const unsigned s = static_cast<unsigned>(sec) % 60;
    if (h > 0) {
        if (m == 0) snprintf(buf, n, "%u hr", h);
        else snprintf(buf, n, "%u hr %u min", h, m);
        return;
    }
    if (sec < 60) {
        snprintf(buf, n, "%u sec", static_cast<unsigned>(sec));
        return;
    }
    if (s == 0) snprintf(buf, n, "%u min", m);
    else snprintf(buf, n, "%u min %u sec", m, s);
}
}  // namespace

void FleetScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen_, 0, LV_PART_MAIN);
    paxx_disable_input(screen_);

    titleBar_ = lv_obj_create(screen_);
    lv_obj_set_size(titleBar_, LV_PCT(100), 48);
    lv_obj_align(titleBar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(titleBar_, PaxxTheme::bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(titleBar_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(titleBar_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(titleBar_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(titleBar_, 16, LV_PART_MAIN);
    paxx_disable_input(titleBar_);

    titleLbl_ = lv_label_create(titleBar_);
    lv_label_set_text(titleLbl_, "PandaFarm");
    lv_obj_set_style_text_font(titleLbl_, PaxxTheme::fontTitle(), LV_PART_MAIN);
    lv_obj_set_width(titleLbl_, 420);
    lv_label_set_long_mode(titleLbl_, LV_LABEL_LONG_CLIP);
    lv_obj_align(titleLbl_, LV_ALIGN_LEFT_MID, 0, 0);
    paxx_mark_accent_text(titleLbl_);
    lv_obj_set_style_text_color(titleLbl_, PaxxTheme::accent(), LV_PART_MAIN);

    lv_obj_t *addBtn = lv_button_create(titleBar_);
    lv_obj_set_size(addBtn, 40, 40);
    lv_obj_align(addBtn, LV_ALIGN_RIGHT_MID, -56, 0);
    lv_obj_add_event_cb(addBtn, [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->addPrinter();
    }, LV_EVENT_CLICKED, app);
    paxx_set_centered_icon(addBtn, LV_SYMBOL_PLUS);
    paxx_mark_accent_fill(addBtn);
    lv_obj_set_style_bg_color(addBtn, PaxxTheme::accent(), LV_PART_MAIN);

    header_ = lv_obj_create(screen_);
    lv_obj_set_size(header_, LV_PCT(100), 36);
    lv_obj_align(header_, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(header_, PaxxTheme::header(), LV_PART_MAIN);
    lv_obj_set_style_border_width(header_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(header_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(header_, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(header_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header_, 8, LV_PART_MAIN);

    auto makeHdr = [&](const char *text, int w, lv_event_cb_t cb, lv_obj_t **store) {
        lv_obj_t *btn = lv_button_create(header_);
        lv_obj_set_size(btn, w, 28);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, PaxxTheme::muted(), LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        if (store) *store = lbl;
        return btn;
    };
    makeHdr("Device Name " LV_SYMBOL_UP " " LV_SYMBOL_DOWN, kColName, onSortName, &nameHdr_);
    makeHdr("Task Name", kColTask, nullptr, nullptr);
    makeHdr("Device Status " LV_SYMBOL_UP " " LV_SYMBOL_DOWN, 220, onSortStatus, &statusHdr_);

    list_ = lv_obj_create(screen_);
    lv_obj_set_size(list_, LV_PCT(100), 396);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(list_, PaxxTheme::row(), LV_PART_MAIN);
    lv_obj_set_style_border_width(list_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(list_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list_, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
}

void FleetScreen::onSortName(lv_event_t *e) {
    auto *self = static_cast<FleetScreen *>(lv_event_get_user_data(e));
    self->app_->config().fleetSort = FleetSort::DeviceName;
    self->app_->saveConfig();
    self->rebuild();
}

void FleetScreen::onSortStatus(lv_event_t *e) {
    auto *self = static_cast<FleetScreen *>(lv_event_get_user_data(e));
    self->app_->config().fleetSort = FleetSort::DeviceStatus;
    self->app_->saveConfig();
    self->rebuild();
}

void FleetScreen::onEnter() {
    if (screen_) lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    if (titleBar_) lv_obj_set_style_bg_color(titleBar_, PaxxTheme::bg(), LV_PART_MAIN);
    if (titleLbl_) {
        lv_obj_set_style_text_font(titleLbl_, PaxxTheme::fontTitle(), LV_PART_MAIN);
        lv_obj_set_style_text_color(titleLbl_, PaxxTheme::accent(), LV_PART_MAIN);
    }
    if (header_) {
        lv_obj_set_style_bg_color(header_, PaxxTheme::header(), LV_PART_MAIN);
        const uint32_t n = lv_obj_get_child_count(header_);
        for (uint32_t i = 0; i < n; ++i) {
            lv_obj_t *btn = lv_obj_get_child(header_, i);
            const uint32_t m = lv_obj_get_child_count(btn);
            for (uint32_t j = 0; j < m; ++j) {
                lv_obj_t *lbl = lv_obj_get_child(btn, j);
                lv_obj_set_style_text_color(lbl, PaxxTheme::muted(), LV_PART_MAIN);
                lv_obj_set_style_text_font(lbl, PaxxTheme::fontBody(), LV_PART_MAIN);
            }
        }
    }
    if (list_) lv_obj_set_style_bg_color(list_, PaxxTheme::row(), LV_PART_MAIN);
    rebuild();
}

void FleetScreen::onTick() {
    if (millis() - lastUiMs_ < 800) return;
    lastUiMs_ = millis();
    rebuild();
}

void FleetScreen::rebuild() {
    if (!list_ || !app_) return;
    lv_obj_clean(list_);

    int count = 0;
    int n = 0;
    BambuFleet::instance().snapshot(profiles_, live_, &count);
    BambuFleet::instance().sortedIndexes(order_, &n, app_->config().fleetSort);
    if (n > count) n = count;

    if (n == 0) {
        lv_obj_t *empty = lv_label_create(list_);
        lv_label_set_text(empty, "No printers yet. Tap + or Gear -> Printers.\nDiscover on the LAN from printer setup.");
        lv_obj_set_style_text_color(empty, PaxxTheme::muted(), LV_PART_MAIN);
        lv_obj_set_style_pad_all(empty, 24, LV_PART_MAIN);
        return;
    }

    for (int r = 0; r < n; ++r) {
        const int i = order_[r];
        const PrinterProfile &p = profiles_[i];
        const PrinterLive &st = live_[i];

        lv_obj_t *row = lv_button_create(list_);
        lv_obj_set_size(row, LV_PCT(100), PaxxTheme::rowHeight());
        lv_obj_set_style_bg_color(row, (r % 2) ? PaxxTheme::rowAlt() : PaxxTheme::row(), LV_PART_MAIN);
        lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);
        lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(row, [](lv_event_t *e) {
            auto *app = static_cast<CupboardApp *>(lv_event_get_user_data(e));
            auto *tgt = static_cast<lv_obj_t *>(lv_event_get_target(e));
            const int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(tgt)));
            app->showPrinter(idx);
        }, LV_EVENT_CLICKED, app_);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, p.name[0] ? p.name : "Printer");
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(name, PaxxTheme::fontBody(), LV_PART_MAIN);
        lv_obj_set_style_text_color(name, PaxxTheme::text(), LV_PART_MAIN);
        lv_obj_set_width(name, kColName - 8);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *task = lv_label_create(row);
        const char *taskText = "No task";
        if (st.taskName[0] && bambuStateIsActive(st.state)) taskText = st.taskName;
        else if (st.taskName[0] && st.online && st.state != BambuGcodeState::Idle) taskText = st.taskName;
        lv_label_set_text(task, taskText);
        lv_label_set_long_mode(task, LV_LABEL_LONG_DOT);
        lv_obj_set_width(task, kColTask - 8);
        lv_obj_align(task, LV_ALIGN_LEFT_MID, kColName, 0);
        lv_obj_set_style_text_font(task, PaxxTheme::fontBody(), LV_PART_MAIN);
        lv_obj_set_style_text_color(task, PaxxTheme::muted(), LV_PART_MAIN);

        char status[48];
        int pct = -1;
        StatusTone tone = StatusTone::Normal;
        fillStatus(st, p, status, sizeof(status), &pct, &tone);

        lv_obj_t *statusBox = lv_obj_create(row);
        lv_obj_set_size(statusBox, 240, PaxxTheme::rowHeight() - 8);
        lv_obj_align(statusBox, LV_ALIGN_LEFT_MID, kColName + kColTask, 0);
        lv_obj_set_style_bg_opa(statusBox, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(statusBox, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(statusBox, 0, LV_PART_MAIN);
        paxx_disable_input(statusBox);

        lv_obj_t *stLbl = lv_label_create(statusBox);
        lv_label_set_text(stLbl, status);
        lv_obj_set_style_text_font(stLbl, PaxxTheme::fontBody(), LV_PART_MAIN);
        lv_obj_set_style_text_color(stLbl, statusToneColor(tone), LV_PART_MAIN);
        lv_obj_align(stLbl, LV_ALIGN_TOP_LEFT, 0, 0);

        if (pct >= 0 && tone == StatusTone::Progress) {
            lv_obj_t *bar = lv_bar_create(statusBox);
            lv_obj_set_size(bar, 220, 10);
            lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x374151), LV_PART_MAIN);
            lv_obj_set_style_bg_color(bar, PaxxTheme::accent(), LV_PART_INDICATOR);
            lv_bar_set_range(bar, 0, 100);
            lv_bar_set_value(bar, pct, LV_ANIM_OFF);
        }
    }
}

namespace {
void styleActionBtn(lv_obj_t *btn, lv_color_t color, int w = 170) {
    lv_obj_set_size(btn, w, 52);
    lv_obj_set_style_bg_color(btn, color, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

lv_obj_t *centeredLabel(lv_obj_t *btn, const char *text) {
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(lbl);
    return lbl;
}

void setBtnEnabled(lv_obj_t *btn, bool on) {
    if (!btn) return;
    if (on) lv_obj_remove_state(btn, LV_STATE_DISABLED);
    else lv_obj_add_state(btn, LV_STATE_DISABLED);
}
}  // namespace

void DetailScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "Printer", farm_back_fleet_cb, app, true);

    nameLbl_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(nameLbl_, PaxxTheme::fontTitle(), LV_PART_MAIN);
    lv_obj_align(nameLbl_, LV_ALIGN_TOP_LEFT, 24, 64);
    lv_label_set_text(nameLbl_, "Printer");

    statusLbl_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(statusLbl_, PaxxTheme::accent(), LV_PART_MAIN);
    lv_obj_align(statusLbl_, LV_ALIGN_TOP_RIGHT, -24, 68);
    lv_label_set_text(statusLbl_, "");

    taskLbl_ = lv_label_create(screen_);
    lv_obj_set_width(taskLbl_, 752);
    lv_label_set_long_mode(taskLbl_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(taskLbl_, PaxxTheme::muted(), LV_PART_MAIN);
    lv_obj_align(taskLbl_, LV_ALIGN_TOP_LEFT, 24, 96);
    lv_label_set_text(taskLbl_, "No task");

    percentLbl_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(percentLbl_, PaxxTheme::fontLarge(), LV_PART_MAIN);
    lv_obj_align(percentLbl_, LV_ALIGN_TOP_LEFT, 24, 140);
    lv_label_set_text(percentLbl_, "--%");

    remainLbl_ = lv_label_create(screen_);
    lv_obj_align(remainLbl_, LV_ALIGN_TOP_LEFT, 140, 144);
    lv_label_set_text(remainLbl_, "");

    layerLbl_ = lv_label_create(screen_);
    lv_obj_align(layerLbl_, LV_ALIGN_TOP_RIGHT, -24, 144);
    lv_label_set_text(layerLbl_, "");

    bar_ = lv_bar_create(screen_);
    lv_obj_set_size(bar_, 752, 18);
    lv_obj_align(bar_, LV_ALIGN_TOP_MID, 0, 178);
    lv_obj_set_style_bg_color(bar_, PaxxTheme::header(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, PaxxTheme::accent(), LV_PART_INDICATOR);
    lv_bar_set_range(bar_, 0, 100);
    lv_bar_set_value(bar_, 0, LV_ANIM_OFF);

    tempLbl_ = lv_label_create(screen_);
    lv_obj_align(tempLbl_, LV_ALIGN_TOP_LEFT, 24, 214);
    lv_label_set_text(tempLbl_, "Nozzle -- C    Bed -- C");

    metaLbl_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(metaLbl_, PaxxTheme::muted(), LV_PART_MAIN);
    lv_obj_align(metaLbl_, LV_ALIGN_TOP_LEFT, 24, 246);
    lv_label_set_text(metaLbl_, "");

    pauseBtn_ = lv_button_create(screen_);
    styleActionBtn(pauseBtn_, PaxxTheme::primary(), 230);
    lv_obj_align(pauseBtn_, LV_ALIGN_TOP_LEFT, 24, 300);
    lv_obj_add_event_cb(pauseBtn_, onPauseResume, LV_EVENT_CLICKED, this);
    pauseLbl_ = centeredLabel(pauseBtn_, "Pause");

    stopBtn_ = lv_button_create(screen_);
    styleActionBtn(stopBtn_, PaxxTheme::danger(), 230);
    lv_obj_align(stopBtn_, LV_ALIGN_TOP_MID, 0, 300);
    lv_obj_add_event_cb(stopBtn_, onStop, LV_EVENT_CLICKED, this);
    centeredLabel(stopBtn_, "Stop");

    reprintBtn_ = lv_button_create(screen_);
    styleActionBtn(reprintBtn_, PaxxTheme::accent(), 230);
    lv_obj_align(reprintBtn_, LV_ALIGN_TOP_RIGHT, -24, 300);
    lv_obj_add_event_cb(reprintBtn_, onReprint, LV_EVENT_CLICKED, this);
    centeredLabel(reprintBtn_, "Reprint");

    lv_obj_t *edit = lv_button_create(screen_);
    styleActionBtn(edit, PaxxTheme::surface(), 230);
    lv_obj_align(edit, LV_ALIGN_TOP_LEFT, 24, 368);
    lv_obj_add_event_cb(edit, onEdit, LV_EVENT_CLICKED, this);
    centeredLabel(edit, "Connection");

    confirm_ = lv_obj_create(screen_);
    lv_obj_set_size(confirm_, 420, 180);
    lv_obj_center(confirm_);
    lv_obj_set_style_bg_color(confirm_, PaxxTheme::surface(), LV_PART_MAIN);
    lv_obj_set_style_radius(confirm_, 12, LV_PART_MAIN);
    lv_obj_add_flag(confirm_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *q = lv_label_create(confirm_);
    lv_label_set_text(q, "Stop this print?");
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *yes = lv_button_create(confirm_);
    lv_obj_set_size(yes, 140, 44);
    lv_obj_align(yes, LV_ALIGN_BOTTOM_LEFT, 24, -20);
    lv_obj_set_style_bg_color(yes, PaxxTheme::danger(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(yes, 0, LV_PART_MAIN);
    lv_obj_set_flex_align(yes, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(yes, [](lv_event_t *e) {
        auto *self = static_cast<DetailScreen *>(lv_event_get_user_data(e));
        const int idx = self->app_->config().editIndex;
        if (BambuFleet::instance().sendPrintCommand(idx, "stop")) {
            PaxxNotify::show("Printer", "Stop sent");
        }
        self->setStopConfirm(false);
    }, LV_EVENT_CLICKED, this);
    centeredLabel(yes, "Stop");

    lv_obj_t *no = lv_button_create(confirm_);
    lv_obj_set_size(no, 140, 44);
    lv_obj_align(no, LV_ALIGN_BOTTOM_RIGHT, -24, -20);
    lv_obj_set_style_pad_all(no, 0, LV_PART_MAIN);
    lv_obj_set_flex_align(no, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(no, [](lv_event_t *e) {
        static_cast<DetailScreen *>(lv_event_get_user_data(e))->setStopConfirm(false);
    }, LV_EVENT_CLICKED, this);
    centeredLabel(no, "Cancel");
}

void DetailScreen::onEnter() {
    if (screen_) lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    if (nameLbl_) {
        lv_obj_set_style_text_font(nameLbl_, PaxxTheme::fontTitle(), LV_PART_MAIN);
        lv_obj_set_style_text_color(nameLbl_, PaxxTheme::text(), LV_PART_MAIN);
    }
    if (percentLbl_) lv_obj_set_style_text_font(percentLbl_, PaxxTheme::fontLarge(), LV_PART_MAIN);
    if (taskLbl_) lv_obj_set_style_text_color(taskLbl_, PaxxTheme::muted(), LV_PART_MAIN);
    if (metaLbl_) lv_obj_set_style_text_color(metaLbl_, PaxxTheme::muted(), LV_PART_MAIN);
    if (bar_) {
        lv_obj_set_style_bg_color(bar_, PaxxTheme::header(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar_, PaxxTheme::accent(), LV_PART_INDICATOR);
    }
    if (pauseBtn_) styleActionBtn(pauseBtn_, PaxxTheme::primary(), 230);
    if (stopBtn_) styleActionBtn(stopBtn_, PaxxTheme::danger(), 230);
    if (reprintBtn_) styleActionBtn(reprintBtn_, PaxxTheme::accent(), 230);
    if (confirm_) lv_obj_set_style_bg_color(confirm_, PaxxTheme::surface(), LV_PART_MAIN);
    setStopConfirm(false);
    lastUiMs_ = 0;
    refresh();
}

void DetailScreen::onTick() {
    if (millis() - lastUiMs_ < 500) return;
    lastUiMs_ = millis();
    refresh();
}

void DetailScreen::setStopConfirm(bool visible) {
    if (!confirm_) return;
    if (visible) lv_obj_remove_flag(confirm_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(confirm_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(confirm_);
}

void DetailScreen::refresh() {
    if (!app_ || !nameLbl_) return;
    const int idx = app_->config().editIndex;
    PrinterProfile p{};
    PrinterLive st{};
    if (!BambuFleet::instance().getPrinter(idx, &p, &st)) return;

    lv_label_set_text(nameLbl_, p.name[0] ? p.name : "Printer");
    lv_label_set_text(taskLbl_, st.taskName[0] ? st.taskName : "No task");

    char status[48];
    int pct = -1;
    StatusTone tone = StatusTone::Normal;
    fillStatus(st, p, status, sizeof(status), &pct, &tone);
    lv_label_set_text(statusLbl_, status);
    lv_obj_set_style_text_color(statusLbl_, statusToneColor(tone), LV_PART_MAIN);

    if (pct >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(percentLbl_, buf);
        lv_bar_set_value(bar_, pct, LV_ANIM_OFF);
    } else {
        lv_label_set_text(percentLbl_, "--%");
        lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
    }

    char remain[32] = {};
    bambuFormatRemain(st.remainingMin, remain, sizeof(remain));
    lv_label_set_text(remainLbl_, remain[0] ? remain : "");

    char layer[40] = {};
    if (st.layer >= 0 && st.layerTotal > 0) snprintf(layer, sizeof(layer), "Layer %d / %d", st.layer, st.layerTotal);
    else if (st.layer >= 0) snprintf(layer, sizeof(layer), "Layer %d", st.layer);
    lv_label_set_text(layerLbl_, layer);

    char temps[80];
    if (st.nozzleTemp >= 0 && st.nozzleTarget >= 0 && st.bedTemp >= 0 && st.bedTarget >= 0) {
        snprintf(temps, sizeof(temps), "Nozzle %d/%d C     Bed %d/%d C",
                 st.nozzleTemp, st.nozzleTarget, st.bedTemp, st.bedTarget);
    } else if (st.nozzleTemp >= 0 || st.bedTemp >= 0) {
        snprintf(temps, sizeof(temps), "Nozzle %d C     Bed %d C",
                 st.nozzleTemp >= 0 ? st.nozzleTemp : 0, st.bedTemp >= 0 ? st.bedTemp : 0);
    } else {
        snprintf(temps, sizeof(temps), "Nozzle -- C     Bed -- C");
    }
    lv_label_set_text(tempLbl_, temps);

    char meta[160];
    if (printerIsKlipper(p)) {
        if (st.speedPct >= 0) {
            snprintf(meta, sizeof(meta), "%s:%u    Klipper    Speed %d%%",
                     p.ip, printerListenPort(p), st.speedPct);
        } else {
            snprintf(meta, sizeof(meta), "%s:%u    Klipper", p.ip, printerListenPort(p));
        }
    } else if (st.speedPct >= 0) {
        snprintf(meta, sizeof(meta), "%s    %s    %s    Speed %d%%",
                 p.ip, p.serial[0] ? p.serial : "no serial",
                 st.wifi[0] ? st.wifi : "", st.speedPct);
    } else {
        snprintf(meta, sizeof(meta), "%s    %s    %s",
                 p.ip, p.serial[0] ? p.serial : "no serial",
                 st.wifi[0] ? st.wifi : "");
    }
    lv_label_set_text(metaLbl_, meta);

    const bool online = st.online && st.state != BambuGcodeState::Offline;
    const bool paused = st.state == BambuGcodeState::Pause;
    const bool running = st.state == BambuGcodeState::Running || st.state == BambuGcodeState::Prepare ||
                         st.state == BambuGcodeState::Slicing;
    lv_label_set_text(pauseLbl_, paused ? "Resume" : "Pause");
    setBtnEnabled(pauseBtn_, online && (paused || running));
    setBtnEnabled(stopBtn_, online && (paused || running));
    const bool canReprint = online && st.gcodeFile[0] &&
                            (st.state == BambuGcodeState::Idle || st.state == BambuGcodeState::Finish ||
                             st.state == BambuGcodeState::Failed || st.state == BambuGcodeState::Stopped);
    setBtnEnabled(reprintBtn_, canReprint);
}

void DetailScreen::onPauseResume(lv_event_t *e) {
    auto *self = static_cast<DetailScreen *>(lv_event_get_user_data(e));
    const int idx = self->app_->config().editIndex;
    PrinterProfile p{};
    PrinterLive st{};
    if (!BambuFleet::instance().getPrinter(idx, &p, &st)) return;
    const char *cmd = st.state == BambuGcodeState::Pause ? "resume" : "pause";
    if (BambuFleet::instance().sendPrintCommand(idx, cmd)) {
        PaxxNotify::show("Printer", st.state == BambuGcodeState::Pause ? "Resume sent" : "Pause sent");
    }
}

void DetailScreen::onStop(lv_event_t *e) {
    static_cast<DetailScreen *>(lv_event_get_user_data(e))->setStopConfirm(true);
}

void DetailScreen::onReprint(lv_event_t *e) {
    auto *self = static_cast<DetailScreen *>(lv_event_get_user_data(e));
    const int idx = self->app_->config().editIndex;
    if (BambuFleet::instance().reprintLast(idx)) PaxxNotify::show("Printer", "Reprint sent");
    else PaxxNotify::show("Printer", "No file to reprint");
}

void DetailScreen::onEdit(lv_event_t *e) {
    auto *self = static_cast<DetailScreen *>(lv_event_get_user_data(e));
    self->app_->editPrinter(self->app_->config().editIndex);
}

void TypeSelectScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "Printer Type", farm_back_fleet_cb, app, true);

    lv_obj_t *hint = lv_label_create(screen_);
    paxx_set_form_width(hint);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_text_color(hint, PaxxTheme::muted(), LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, "Choose the printer type, then enter connection details.");

    auto addChoice = [&](const char *title, const char *subtitle, PrinterType type, int y) {
        lv_obj_t *btn = lv_button_create(screen_);
        paxx_set_form_width(btn);
        lv_obj_set_height(btn, 96);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_set_layout(btn, LV_LAYOUT_NONE);
        lv_obj_set_style_bg_color(btn, PaxxTheme::surface(), LV_PART_MAIN);
        lv_obj_set_style_pad_hor(btn, 24, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(btn, 16, LV_PART_MAIN);
        lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(type)));
        lv_obj_add_event_cb(btn, onPick, LV_EVENT_CLICKED, this);

        lv_obj_t *t = lv_label_create(btn);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_font(t, PaxxTheme::fontTitle(), LV_PART_MAIN);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 4);

        lv_obj_t *s = lv_label_create(btn);
        lv_label_set_text(s, subtitle);
        lv_obj_set_style_text_color(s, PaxxTheme::muted(), LV_PART_MAIN);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    };

    addChoice("Bambu Lab", "X1, P1, A1, and other Bambu printers  —  IP + LAN access code",
              PrinterType::BambuLab, 120);
    addChoice("Klipper", "Moonraker / Mainsail / Fluidd  —  IP + optional API key",
              PrinterType::Klipper, 232);
}

void TypeSelectScreen::onEnter() {
    if (!screen_) return;
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    const uint32_t n = lv_obj_get_child_count(screen_);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t *child = lv_obj_get_child(screen_, i);
        if (lv_obj_check_type(child, &lv_button_class)) {
            lv_obj_set_style_bg_color(child, PaxxTheme::surface(), LV_PART_MAIN);
        }
    }
}

void TypeSelectScreen::onPick(lv_event_t *e) {
    auto *self = static_cast<TypeSelectScreen *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_current_target_obj(e);
    if (!self || !self->app_ || !btn) return;
    const auto type = static_cast<PrinterType>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn)));
    self->app_->addPrinter(type);
}

void SetupScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "Printer Setup", farm_back_fleet_cb, app, true);

    typeLbl_ = lv_label_create(screen_);
    paxx_set_form_width(typeLbl_);
    lv_obj_set_style_text_color(typeLbl_, PaxxTheme::muted(), LV_PART_MAIN);
    lv_obj_set_style_text_align(typeLbl_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(typeLbl_, "Bambu Lab");

    auto addField = [&](lv_obj_t **ta, const char *ph, PaxxKbMode mode) {
        *ta = lv_textarea_create(screen_);
        styleTa(*ta);
        lv_textarea_set_placeholder_text(*ta, ph);
        PaxxKeyboard::attach(*ta, mode);
    };

    addField(&nameTa_, "Printer name (e.g. A1 Mini AMS)", PaxxKbMode::Text);
    addField(&ipTa_, "IP address (e.g. 192.168.1.50)", PaxxKbMode::Number);
    addField(&portTa_, "Moonraker port (7125)", PaxxKbMode::Number);
    addField(&codeTa_, "LAN access code", PaxxKbMode::Password);
    addField(&serialTa_, "Serial (optional — auto from printer)", PaxxKbMode::Text);

    hintLbl_ = lv_label_create(screen_);
    paxx_set_form_width(hintLbl_);
    lv_obj_set_style_text_color(hintLbl_, PaxxTheme::muted(), LV_PART_MAIN);
    lv_label_set_long_mode(hintLbl_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hintLbl_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(hintLbl_, "Access code is on the printer under Settings > WLAN. Cloud mode is fine — LAN Only is not required.");

    scanBtn_ = lv_button_create(screen_);
    paxx_set_form_width(scanBtn_);
    lv_obj_set_height(scanBtn_, 40);
    lv_obj_add_event_cb(scanBtn_, [](lv_event_t *e) {
        auto *self = static_cast<SetupScreen *>(lv_event_get_user_data(e));
        self->app_->startDiscovery();
        self->refreshDiscoverList();
    }, LV_EVENT_CLICKED, this);
    lv_label_set_text(lv_label_create(scanBtn_), LV_SYMBOL_REFRESH "  Discover on LAN");

    discoverList_ = lv_list_create(screen_);
    paxx_set_form_width(discoverList_);
    lv_obj_set_height(discoverList_, 90);

    saveBtn_ = lv_button_create(screen_);
    paxx_set_form_width(saveBtn_);
    lv_obj_set_height(saveBtn_, 44);
    lv_obj_add_event_cb(saveBtn_, [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->savePrinterFromSetup();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(saveBtn_), "Save printer");
}

void SetupScreen::applyTypeLayout() {
    if (!app_ || !screen_) return;
    const AppConfig &cfg = app_->config();
    const PrinterProfile empty{};
    const PrinterProfile &p =
        (cfg.editIndex >= 0 && cfg.editIndex < cfg.printerCount) ? cfg.printers[cfg.editIndex] : empty;
    const bool klipper = printerIsKlipper(p);
    int y = 56;

    auto place = [&](lv_obj_t *obj, bool show, int step) {
        if (!obj) return;
        if (show) {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
            y += step;
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    };

    if (typeLbl_) {
        lv_label_set_text(typeLbl_, klipper ? "Klipper  —  Moonraker HTTP" : "Bambu Lab  —  LAN MQTT");
    }
    if (codeTa_) {
        lv_textarea_set_placeholder_text(codeTa_, klipper ? "API key (optional)" : "LAN access code");
    }
    if (hintLbl_) {
        lv_label_set_text(hintLbl_,
                          klipper
                              ? "Moonraker default port is 7125. API key is optional if this panel is a trusted client."
                              : "Access code is on the printer under Settings > WLAN. Cloud mode is fine — LAN Only is not required.");
    }

    place(typeLbl_, true, 28);
    place(nameTa_, true, 48);
    place(ipTa_, true, 48);
    place(portTa_, klipper, 48);
    place(codeTa_, true, 48);
    place(serialTa_, !klipper, 48);
    place(hintLbl_, true, 40);
    place(scanBtn_, !klipper, 48);
    place(discoverList_, !klipper, 100);
    place(saveBtn_, true, 48);
}

void SetupScreen::loadFromEdit() {
    if (!app_) return;
    const AppConfig &cfg = app_->config();
    const PrinterProfile empty{};
    const PrinterProfile &p =
        (cfg.editIndex >= 0 && cfg.editIndex < cfg.printerCount) ? cfg.printers[cfg.editIndex] : empty;
    if (nameTa_) lv_textarea_set_text(nameTa_, p.name);
    if (ipTa_) lv_textarea_set_text(ipTa_, p.ip);
    if (codeTa_) lv_textarea_set_text(codeTa_, p.accessCode);
    if (serialTa_) lv_textarea_set_text(serialTa_, p.serial);
    if (portTa_) {
        if (printerIsKlipper(p)) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(printerListenPort(p)));
            lv_textarea_set_text(portTa_, buf);
        } else {
            lv_textarea_set_text(portTa_, "");
        }
    }
    applyTypeLayout();
}

void SetupScreen::applyDiscovery(const DiscoveredPrinter &d) {
    if (ipTa_ && d.ip[0]) lv_textarea_set_text(ipTa_, d.ip);
    if (serialTa_ && d.serial[0]) lv_textarea_set_text(serialTa_, d.serial);
    if (nameTa_ && d.name[0]) lv_textarea_set_text(nameTa_, d.name);
    if (hintLbl_) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Found %s — enter the LAN access code, then Save",
                 d.model[0] ? d.model : d.name);
        lv_label_set_text(hintLbl_, buf);
    }
    if (codeTa_) PaxxKeyboard::promptFor(codeTa_);
}

void SetupScreen::refreshDiscoverList() {
    if (!discoverList_ || !app_) return;
    lv_obj_clean(discoverList_);
    const auto &found = app_->discovery().results();
    if (found.empty()) {
        lv_list_add_text(discoverList_, app_->discovery().isScanning() ? "Scanning..." : "No printers found yet");
        return;
    }
    for (const auto &d : found) {
        char label[80];
        snprintf(label, sizeof(label), "%s  %s", d.name[0] ? d.name : "Bambu", d.ip);
        lv_obj_t *btn = lv_list_add_button(discoverList_, LV_SYMBOL_PLUS, label);
        auto *copy = new DiscoveredPrinter(d);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            auto *self = static_cast<SetupScreen *>(lv_event_get_user_data(e));
            auto *d = static_cast<DiscoveredPrinter *>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(e))));
            if (d) {
                self->applyDiscovery(*d);
                delete d;
            }
        }, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(btn, copy);
    }
}

void SetupScreen::onEnter() {
    loadFromEdit();
    refreshDiscoverList();
    if (ipTa_ && (!lv_textarea_get_text(ipTa_) || !lv_textarea_get_text(ipTa_)[0])) {
        PaxxKeyboard::promptFor(ipTa_);
    }
}

void PrinterManagerScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "Printers", farm_back_fleet_cb, app, true);

    hintLbl_ = lv_label_create(screen_);
    paxx_set_form_width(hintLbl_);
    lv_obj_align(hintLbl_, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_color(hintLbl_, PaxxTheme::muted(), LV_PART_MAIN);

    list_ = lv_obj_create(screen_);
    paxx_set_form_width(list_);
    lv_obj_set_height(list_, 320);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list_, 0, LV_PART_MAIN);

    lv_obj_t *add = lv_button_create(screen_);
    paxx_set_form_width(add);
    lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(add, [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->addPrinter();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(add), LV_SYMBOL_PLUS "  Add printer");
}

void PrinterManagerScreen::onEnter() { rebuildList(); }

void PrinterManagerScreen::rebuildList() {
    if (!list_ || !app_) return;
    lv_obj_clean(list_);
    rowCtxs_.clear();
    AppConfig &cfg = app_->config();
    if (hintLbl_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d / %d printers", cfg.printerCount, BAMBU_MAX_PRINTERS);
        lv_label_set_text(hintLbl_, buf);
    }
    rowCtxs_.reserve(static_cast<size_t>(cfg.printerCount));
    for (int i = 0; i < cfg.printerCount; ++i) {
        rowCtxs_.push_back(RowCtx{app_, i});
        RowCtx *ctx = &rowCtxs_.back();
        const PrinterProfile &p = cfg.printers[i];

        lv_obj_t *row = lv_obj_create(list_);
        lv_obj_set_size(row, LV_PCT(100), 64);
        lv_obj_set_style_bg_color(row, PaxxTheme::surface(), LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, 10, LV_PART_MAIN);
        paxx_disable_input(row);

        lv_obj_t *info = lv_obj_create(row);
        lv_obj_set_size(info, 480, 52);
        lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(info, 0, LV_PART_MAIN);
        paxx_disable_input(info);
        lv_obj_t *n = lv_label_create(info);
        lv_label_set_text(n, p.name[0] ? p.name : "Printer");
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 4);
        lv_obj_t *h = lv_label_create(info);
        char sub[64];
        if (!p.ip[0]) snprintf(sub, sizeof(sub), "%s  No IP", printerTypeLabel(p.type));
        else if (printerIsKlipper(p)) {
            snprintf(sub, sizeof(sub), "%s  %s:%u", printerTypeLabel(p.type), p.ip, printerListenPort(p));
        } else {
            snprintf(sub, sizeof(sub), "%s  %s", printerTypeLabel(p.type), p.ip);
        }
        lv_label_set_text(h, sub);
        lv_obj_set_style_text_color(h, PaxxTheme::muted(), LV_PART_MAIN);
        lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, 0, -4);

        lv_obj_t *edit = lv_button_create(row);
        lv_obj_set_size(edit, 56, 40);
        lv_obj_add_event_cb(edit, [](lv_event_t *e) {
            auto *c = static_cast<RowCtx *>(lv_event_get_user_data(e));
            c->app->editPrinter(c->index);
        }, LV_EVENT_CLICKED, ctx);
        lv_label_set_text(lv_label_create(edit), LV_SYMBOL_EDIT);

        lv_obj_t *del = lv_button_create(row);
        lv_obj_set_size(del, 56, 40);
        lv_obj_set_style_bg_color(del, PaxxTheme::danger(), LV_PART_MAIN);
        lv_obj_add_event_cb(del, [](lv_event_t *e) {
            auto *c = static_cast<RowCtx *>(lv_event_get_user_data(e));
            if (c->app->removePrinter(c->index)) c->app->printerManager().rebuildList();
        }, LV_EVENT_CLICKED, ctx);
        lv_label_set_text(lv_label_create(del), LV_SYMBOL_TRASH);
    }
}

void WifiScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "WiFi Setup", farm_back_fleet_cb, app, true, &navBackBtn_);

    statusLbl_ = lv_label_create(screen_);
    paxx_set_form_width(statusLbl_);
    lv_obj_align(statusLbl_, LV_ALIGN_TOP_MID, 0, 52);
    lv_label_set_long_mode(statusLbl_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(statusLbl_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(statusLbl_, "Scanning...");

    networkList_ = lv_list_create(screen_);
    paxx_set_form_width(networkList_);
    lv_obj_set_height(networkList_, 130);
    lv_obj_align(networkList_, LV_ALIGN_TOP_MID, 0, 78);

    passTa_ = lv_textarea_create(screen_);
    paxx_set_form_width(passTa_);
    lv_obj_align(passTa_, LV_ALIGN_TOP_MID, 0, 218);
    lv_textarea_set_password_mode(passTa_, true);
    lv_textarea_set_one_line(passTa_, true);
    lv_textarea_set_placeholder_text(passTa_, "WiFi password");
    PaxxKeyboard::attach(passTa_, PaxxKbMode::Password);

    lv_obj_t *connect = lv_button_create(screen_);
    paxx_set_form_width(connect);
    lv_obj_align(connect, LV_ALIGN_TOP_MID, 0, 272);
    lv_obj_add_event_cb(connect, [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->wifiScreen().connectSelected();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(connect), "Join Network");

    lv_obj_t *rescan = lv_button_create(screen_);
    paxx_set_form_width(rescan);
    lv_obj_align(rescan, LV_ALIGN_TOP_MID, 0, 318);
    lv_obj_add_event_cb(rescan, [](lv_event_t *e) {
        if (PaxxKeyboard::isVisible()) PaxxKeyboard::hide();
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->wifiScreen().scanNetworks();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(rescan), LV_SYMBOL_REFRESH " Scan again");

    lv_obj_t *forget = lv_button_create(screen_);
    paxx_set_form_width(forget);
    lv_obj_align(forget, LV_ALIGN_TOP_MID, 0, 364);
    lv_obj_set_style_bg_color(forget, PaxxTheme::danger(), LV_PART_MAIN);
    lv_obj_add_event_cb(forget, [](lv_event_t *e) {
        PaxxKeyboard::hide();
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->wifiScreen().forgetAllNetworks();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(forget), "Forget All Networks");
}

void WifiScreen::setStatus(const char *text) {
    if (!statusLbl_) return;
    const char *msg = text ? text : "";
    const char *cur = lv_label_get_text(statusLbl_);
    if (cur && strcmp(cur, msg) == 0) return;
    lv_label_set_text(statusLbl_, msg);
}

void WifiScreen::selectNetwork(size_t index) {
    if (index >= networks_.size()) return;
    selectedIndex_ = static_cast<int>(index);
    const WifiNetwork &net = networks_[index];
    if (!net.secure) {
        PaxxKeyboard::hide();
        strlcpy(app_->config().wifi.ssid, net.ssid, sizeof(app_->config().wifi.ssid));
        app_->config().wifi.password[0] = '\0';
        app_->saveConfig();
        setStatus("Connecting...");
        app_->showGlobalLoading(true, "Connecting to WiFi...");
        app_->wifi().startConnect(net.ssid, "", 15);
        return;
    }
    lv_textarea_set_text(passTa_, "");
    char hint[48];
    snprintf(hint, sizeof(hint), "Password for %s", net.ssid);
    lv_textarea_set_placeholder_text(passTa_, hint);
    setStatus("Enter password, then tap Join Network");
    PaxxKeyboard::promptFor(passTa_);
}

void WifiScreen::connectSelected() {
    if (selectedIndex_ < 0 || static_cast<size_t>(selectedIndex_) >= networks_.size()) {
        setStatus("Tap a network first");
        return;
    }
    const WifiNetwork &net = networks_[static_cast<size_t>(selectedIndex_)];
    PaxxKeyboard::hide();
    strlcpy(app_->config().wifi.ssid, net.ssid, sizeof(app_->config().wifi.ssid));
    strlcpy(app_->config().wifi.password, lv_textarea_get_text(passTa_), sizeof(app_->config().wifi.password));
    app_->saveConfig();
    setStatus("Connecting...");
    app_->showGlobalLoading(true, "Connecting to WiFi...");
    app_->wifi().startConnect(app_->config().wifi.ssid, app_->config().wifi.password, 15);
}

void WifiScreen::forgetAllNetworks() {
    app_->config().wifi.ssid[0] = '\0';
    app_->config().wifi.password[0] = '\0';
    app_->saveConfig();
    app_->wifi().forgetAll();
    selectedIndex_ = -1;
    networks_.clear();
    lv_obj_clean(networkList_);
    lv_textarea_set_text(passTa_, "");
    setStatus("Networks cleared - scanning...");
    scanNetworks();
    PaxxNotify::show("WiFi", "Saved networks cleared");
}

void WifiScreen::onEnter() {
    selectedIndex_ = -1;
    lv_textarea_set_text(passTa_, "");
    if (WiFi.isConnected()) setStatus(WiFi.localIP().toString().c_str());
    else setStatus("Scanning...");
    lv_async_call([](void *p) { static_cast<WifiScreen *>(p)->scanNetworks(); }, this);
}

void WifiScreen::scanNetworks() {
    scanning_ = true;
    app_->showGlobalLoading(true, "Scanning WiFi...");
    setStatus("Scanning...");
    paxx_ui_refresh();
    std::vector<WifiNetwork> nets;
    app_->wifi().scan(nets);
    scanning_ = false;
    app_->showGlobalLoading(false);
    applyNetworkList(nets);
}

void WifiScreen::applyNetworkList(const std::vector<WifiNetwork> &nets) {
    networks_ = nets;
    selectedIndex_ = -1;
    lv_obj_clean(networkList_);
    if (networks_.empty()) {
        lv_list_add_text(networkList_, "No networks found");
        setStatus("No networks found");
        return;
    }
    for (size_t i = 0; i < networks_.size(); ++i) {
        char label[64];
        snprintf(label, sizeof(label), "%s  (%d dBm)", networks_[i].ssid, networks_[i].rssi);
        lv_obj_t *btn = lv_list_add_button(networkList_, LV_SYMBOL_WIFI, label);
        lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            auto *self = static_cast<WifiScreen *>(lv_event_get_user_data(e));
            auto *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
            self->selectNetwork(static_cast<size_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target))));
        }, LV_EVENT_CLICKED, this);
    }
    char status[48];
    snprintf(status, sizeof(status), "Found %u - tap a network", static_cast<unsigned>(networks_.size()));
    setStatus(status);
}

void ThemeScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "Theme", farm_back_fleet_cb, app, true);

    lv_obj_t *sizeLbl = lv_label_create(screen_);
    lv_label_set_text(sizeLbl, "Text size");
    lv_obj_align(sizeLbl, LV_ALIGN_TOP_LEFT, 24, 64);

    static const char *kSizes[] = {"S", "M", "L"};
    for (int i = 0; i < 3; ++i) {
        sizeBtns_[i] = lv_button_create(screen_);
        lv_obj_set_size(sizeBtns_[i], 88, 44);
        lv_obj_align(sizeBtns_[i], LV_ALIGN_TOP_LEFT, 24 + i * 100, 96);
        lv_obj_set_user_data(sizeBtns_[i], reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(sizeBtns_[i], onSize, LV_EVENT_CLICKED, this);
        lv_obj_t *lbl = lv_label_create(sizeBtns_[i]);
        lv_label_set_text(lbl, kSizes[i]);
        lv_obj_center(lbl);
    }

    lv_obj_t *themeLbl = lv_label_create(screen_);
    lv_label_set_text(themeLbl, "Theme");
    lv_obj_align(themeLbl, LV_ALIGN_TOP_LEFT, 24, 156);

    const int count = static_cast<int>(UiTheme::Count);
    for (int i = 0; i < count; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        themeBtns_[i] = lv_button_create(screen_);
        lv_obj_set_size(themeBtns_[i], 360, 52);
        lv_obj_align(themeBtns_[i], LV_ALIGN_TOP_LEFT, 24 + col * 384, 188 + row * 60);
        lv_obj_set_user_data(themeBtns_[i], reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(themeBtns_[i], onTheme, LV_EVENT_CLICKED, this);
        lv_obj_set_style_pad_hor(themeBtns_[i], 12, LV_PART_MAIN);
        lv_obj_set_flex_flow(themeBtns_[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(themeBtns_[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(themeBtns_[i], 12, LV_PART_MAIN);

        const UiTheme theme = static_cast<UiTheme>(i);
        lv_obj_t *swatch = lv_obj_create(themeBtns_[i]);
        lv_obj_set_size(swatch, 32, 32);
        lv_obj_set_style_radius(swatch, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_color(swatch, PaxxTheme::preview(theme), LV_PART_MAIN);
        lv_obj_set_style_border_width(swatch, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(swatch, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_pad_all(swatch, 6, LV_PART_MAIN);
        paxx_disable_input(swatch);

        lv_obj_t *dot = lv_obj_create(swatch);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_center(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, PaxxTheme::previewAccent(theme), LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        paxx_disable_input(dot);

        lv_obj_t *name = lv_label_create(themeBtns_[i]);
        lv_label_set_text(name, PaxxTheme::themeName(theme));
    }
}

void ThemeScreen::onEnter() { refresh(); }

void ThemeScreen::refresh() {
    if (!screen_) return;
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    lv_obj_set_style_text_color(screen_, PaxxTheme::text(), LV_PART_MAIN);
    lv_obj_set_style_text_font(screen_, PaxxTheme::fontBody(), LV_PART_MAIN);

    if (lv_obj_t *bar = lv_obj_get_child(screen_, 0)) {
        lv_obj_set_style_bg_color(bar, PaxxTheme::surface(), LV_PART_MAIN);
        const uint32_t n = lv_obj_get_child_count(bar);
        for (uint32_t i = 0; i < n; ++i) {
            lv_obj_t *child = lv_obj_get_child(bar, i);
            if (lv_obj_check_type(child, &lv_label_class)) {
                lv_obj_set_style_text_font(child, PaxxTheme::fontTitle(), LV_PART_MAIN);
                lv_obj_set_style_text_color(child, PaxxTheme::text(), LV_PART_MAIN);
            }
        }
    }

    const int selSize = static_cast<int>(PaxxTheme::textSize());
    for (int i = 0; i < 3; ++i) {
        if (!sizeBtns_[i]) continue;
        lv_obj_set_style_bg_color(sizeBtns_[i],
                                  i == selSize ? PaxxTheme::primary() : PaxxTheme::surface(), LV_PART_MAIN);
        if (lv_obj_t *lbl = lv_obj_get_child(sizeBtns_[i], 0)) {
            lv_obj_set_style_text_font(lbl, PaxxTheme::fontTitle(), LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, PaxxTheme::text(), LV_PART_MAIN);
        }
    }

    const int selTheme = static_cast<int>(PaxxTheme::theme());
    const int count = static_cast<int>(UiTheme::Count);
    for (int i = 0; i < count; ++i) {
        if (!themeBtns_[i]) continue;
        const bool on = i == selTheme;
        lv_obj_set_style_bg_color(themeBtns_[i], on ? PaxxTheme::primary() : PaxxTheme::surface(), LV_PART_MAIN);
        lv_obj_set_style_border_width(themeBtns_[i], on ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(themeBtns_[i], on ? PaxxTheme::accent() : PaxxTheme::header(), LV_PART_MAIN);
        const uint32_t n = lv_obj_get_child_count(themeBtns_[i]);
        for (uint32_t c = 0; c < n; ++c) {
            lv_obj_t *child = lv_obj_get_child(themeBtns_[i], c);
            if (lv_obj_check_type(child, &lv_label_class)) {
                lv_obj_set_style_text_font(child, PaxxTheme::fontBody(), LV_PART_MAIN);
                lv_obj_set_style_text_color(child, PaxxTheme::text(), LV_PART_MAIN);
            }
        }
    }
}

void ThemeScreen::onSize(lv_event_t *e) {
    auto *self = static_cast<ThemeScreen *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_current_target_obj(e);
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn)));
    if (i < 0 || i > static_cast<int>(UiTextSize::Large) || !self->app_) return;
    self->app_->setAppearance(PaxxTheme::theme(), static_cast<UiTextSize>(i));
}

void ThemeScreen::onTheme(lv_event_t *e) {
    auto *self = static_cast<ThemeScreen *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_current_target_obj(e);
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn)));
    if (i < 0 || i >= static_cast<int>(UiTheme::Count) || !self->app_) return;
    self->app_->setAppearance(static_cast<UiTheme>(i), PaxxTheme::textSize());
}

void DisplayScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "Display", farm_back_fleet_cb, app, true);

    auto makeRow = [&](const char *title, const char *hint, int y, lv_obj_t **slider, lv_obj_t **val,
                       int32_t minV, int32_t maxV, int kind) {
        lv_obj_t *lbl = lv_label_create(screen_);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_font(lbl, PaxxTheme::fontTitle(), LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 24, y);

        *val = lv_label_create(screen_);
        lv_obj_align(*val, LV_ALIGN_TOP_RIGHT, -24, y);

        *slider = lv_slider_create(screen_);
        lv_slider_set_range(*slider, minV, maxV);
        lv_obj_set_width(*slider, kPaxxFormWidth);
        lv_obj_align(*slider, LV_ALIGN_TOP_MID, 0, y + 36);
        lv_obj_set_height(*slider, 22);
        lv_obj_set_user_data(*slider, reinterpret_cast<void *>(static_cast<intptr_t>(kind)));
        lv_obj_add_event_cb(*slider, onSlider, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(*slider, onSlider, LV_EVENT_RELEASED, this);
        lv_obj_set_style_bg_color(*slider, PaxxTheme::header(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(*slider, PaxxTheme::primary(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(*slider, PaxxTheme::primary(), LV_PART_KNOB);

        lv_obj_t *hintLbl = lv_label_create(screen_);
        lv_label_set_text(hintLbl, hint);
        lv_obj_set_style_text_color(hintLbl, PaxxTheme::muted(), LV_PART_MAIN);
        lv_obj_align(hintLbl, LV_ALIGN_TOP_LEFT, 24, y + 68);
    };

    makeRow("Brightness", "Live backlight level", 64, &brightSlider_, &brightVal_, 1, 100, 0);
    makeRow("Dim Time", "Dim to 10% after idle. 0 = do not dim", 160, &dimSlider_, &dimVal_, 0, DISPLAY_TIMEOUT_MAX_MIN, 1);
    makeRow("Sleep Time", "Turn off after idle. 0 = keep display on", 270, &sleepSlider_, &sleepVal_, 0, DISPLAY_TIMEOUT_MAX_MIN, 2);
}

void DisplayScreen::onEnter() {
    if (!app_ || !brightSlider_) return;
    loading_ = true;
    lv_slider_set_value(brightSlider_, app_->config().brightness, LV_ANIM_OFF);
    lv_slider_set_value(dimSlider_, app_->config().dimSec / 60, LV_ANIM_OFF);
    lv_slider_set_value(sleepSlider_, app_->config().sleepSec / 60, LV_ANIM_OFF);
    loading_ = false;
    updateLabels();
    refresh();
}

void DisplayScreen::updateLabels() {
    if (brightVal_ && brightSlider_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(lv_slider_get_value(brightSlider_)));
        lv_label_set_text(brightVal_, buf);
    }
    if (dimVal_ && dimSlider_) {
        char buf[24];
        formatTimeout(static_cast<uint16_t>(lv_slider_get_value(dimSlider_) * 60), buf, sizeof(buf));
        lv_label_set_text(dimVal_, buf);
    }
    if (sleepVal_ && sleepSlider_) {
        char buf[24];
        formatTimeout(static_cast<uint16_t>(lv_slider_get_value(sleepSlider_) * 60), buf, sizeof(buf));
        lv_label_set_text(sleepVal_, buf);
    }
}

void DisplayScreen::applyFromSliders(bool save) {
    if (!app_ || !brightSlider_ || !dimSlider_ || !sleepSlider_) return;
    app_->applyDisplaySettings(static_cast<uint8_t>(lv_slider_get_value(brightSlider_)),
                               static_cast<uint16_t>(lv_slider_get_value(dimSlider_) * 60),
                               static_cast<uint16_t>(lv_slider_get_value(sleepSlider_) * 60),
                               save);
    updateLabels();
}

void DisplayScreen::commit() {
    applyFromSliders(true);
}

void DisplayScreen::onSlider(lv_event_t *e) {
    auto *self = static_cast<DisplayScreen *>(lv_event_get_user_data(e));
    if (!self || self->loading_) return;
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) self->applyFromSliders(false);
    else if (code == LV_EVENT_RELEASED) self->applyFromSliders(true);
}

void DisplayScreen::refresh() {
    if (!screen_) return;
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(), LV_PART_MAIN);
    lv_obj_set_style_text_color(screen_, PaxxTheme::text(), LV_PART_MAIN);
    lv_obj_set_style_text_font(screen_, PaxxTheme::fontBody(), LV_PART_MAIN);
    if (lv_obj_t *bar = lv_obj_get_child(screen_, 0)) {
        lv_obj_set_style_bg_color(bar, PaxxTheme::surface(), LV_PART_MAIN);
    }
    auto styleSlider = [](lv_obj_t *slider) {
        if (!slider) return;
        lv_obj_set_style_bg_color(slider, PaxxTheme::header(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, PaxxTheme::primary(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, PaxxTheme::primary(), LV_PART_KNOB);
    };
    styleSlider(brightSlider_);
    styleSlider(dimSlider_);
    styleSlider(sleepSlider_);
    if (brightVal_) lv_obj_set_style_text_color(brightVal_, PaxxTheme::text(), LV_PART_MAIN);
    if (dimVal_) lv_obj_set_style_text_color(dimVal_, PaxxTheme::text(), LV_PART_MAIN);
    if (sleepVal_) lv_obj_set_style_text_color(sleepVal_, PaxxTheme::text(), LV_PART_MAIN);
}

void SettingsScreen::setHint(const char *text) {
    if (hintLbl_) lv_label_set_text(hintLbl_, text ? text : "");
}

void SettingsScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "About", farm_back_fleet_cb, app, true);

    lv_obj_t *body = lv_obj_create(screen_);
    lv_obj_set_size(body, LV_PCT(100), 432);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, 32, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(body, 12, LV_PART_MAIN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    hintLbl_ = lv_label_create(body);
    paxx_set_form_width(hintLbl_);
    lv_label_set_long_mode(hintLbl_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hintLbl_,
                      "PandaFarm " PANDACUPBOARD_VERSION "\n"
                      "PandaTouch / K-Touch farm dashboard for Bambu Lab and Klipper.\n\n"
                      "Created by TechJeeper Designs\n\n"
                      "Latest Changes\n"
                      "- Klipper printers via Moonraker HTTP\n"
                      "- Choose Bambu Lab or Klipper before setup\n"
                      "- Existing printers stay Bambu Lab after upgrade\n"
                      "- Pause, resume, stop, and reprint on Klipper jobs\n\n"
                      "Bambu Lab: IP + LAN access code, MQTT/TLS 8883, SSDP on UDP 1990.\n"
                      "Klipper: Moonraker HTTP (port 7125) with optional API key.\n"
                      "Farm sorts by Device Status; active jobs with higher completion stay on top.");
}
