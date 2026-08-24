#include "ui/App.h"

#include "pt/pt_display.h"
#include "ui/Keyboard.h"
#include "ui/Notify.h"
#include "ui/Theme.h"

#include <WiFi.h>
#include <cstring>
#include <vector>

namespace {
constexpr int kRowH = 48;
constexpr int kColName = 250;
constexpr int kColTask = 270;
const lv_color_t kBarGreen = lv_color_hex(0x22C55E);
const lv_color_t kHeaderBg = lv_color_hex(0x1F2937);
const lv_color_t kRowBg = lv_color_hex(0x111827);
const lv_color_t kRowAlt = lv_color_hex(0x0B1220);

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
    if (live.state == BambuGcodeState::Syncing && !live.online) {
        snprintf(out, outLen, "syncing");
        return;
    }
    if (!live.online || live.state == BambuGcodeState::Offline) {
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
        default: return PaxxTheme::text(true);
    }
}
}  // namespace

void FleetScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(true), LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen_, 0, LV_PART_MAIN);
    paxx_disable_input(screen_);

    lv_obj_t *titleBar = lv_obj_create(screen_);
    lv_obj_set_size(titleBar, LV_PCT(100), 48);
    lv_obj_align(titleBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(titleBar, PaxxTheme::bg(true), LV_PART_MAIN);
    lv_obj_set_style_border_width(titleBar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(titleBar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(titleBar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(titleBar, 16, LV_PART_MAIN);
    paxx_disable_input(titleBar);

    lv_obj_t *title = lv_label_create(titleBar);
    lv_label_set_text(title, "PandaCupboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_width(title, 420);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *addBtn = lv_button_create(titleBar);
    lv_obj_set_size(addBtn, 48, 36);
    lv_obj_align(addBtn, LV_ALIGN_RIGHT_MID, -48, 0);
    lv_obj_add_event_cb(addBtn, [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->addPrinter();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(addBtn), LV_SYMBOL_PLUS);

    lv_obj_t *header = lv_obj_create(screen_);
    lv_obj_set_size(header, LV_PCT(100), 36);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(header, kHeaderBg, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(header, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 8, LV_PART_MAIN);

    auto makeHdr = [&](const char *text, int w, lv_event_cb_t cb, lv_obj_t **store) {
        lv_obj_t *btn = lv_button_create(header);
        lv_obj_set_size(btn, w, 28);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, PaxxTheme::muted(true), LV_PART_MAIN);
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
    lv_obj_set_style_bg_color(list_, kRowBg, LV_PART_MAIN);
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

void FleetScreen::onEnter() { rebuild(); }

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
        lv_obj_set_style_text_color(empty, PaxxTheme::muted(true), LV_PART_MAIN);
        lv_obj_set_style_pad_all(empty, 24, LV_PART_MAIN);
        return;
    }

    for (int r = 0; r < n; ++r) {
        const int i = order_[r];
        const PrinterProfile &p = profiles_[i];
        const PrinterLive &st = live_[i];

        lv_obj_t *row = lv_button_create(list_);
        lv_obj_set_size(row, LV_PCT(100), kRowH);
        lv_obj_set_style_bg_color(row, (r % 2) ? kRowAlt : kRowBg, LV_PART_MAIN);
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
        char nameBuf[48];
        if (!st.online) snprintf(nameBuf, sizeof(nameBuf), "%s(Offline)", p.name[0] ? p.name : "Printer");
        else snprintf(nameBuf, sizeof(nameBuf), "%s", p.name[0] ? p.name : "Printer");
        lv_label_set_text(name, nameBuf);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
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
        lv_obj_set_style_text_color(task, PaxxTheme::muted(true), LV_PART_MAIN);

        char status[48];
        int pct = -1;
        StatusTone tone = StatusTone::Normal;
        fillStatus(st, p, status, sizeof(status), &pct, &tone);

        lv_obj_t *statusBox = lv_obj_create(row);
        lv_obj_set_size(statusBox, 240, 40);
        lv_obj_align(statusBox, LV_ALIGN_LEFT_MID, kColName + kColTask, 0);
        lv_obj_set_style_bg_opa(statusBox, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(statusBox, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(statusBox, 0, LV_PART_MAIN);
        paxx_disable_input(statusBox);

        lv_obj_t *stLbl = lv_label_create(statusBox);
        lv_label_set_text(stLbl, status);
        lv_obj_set_style_text_color(stLbl, statusToneColor(tone), LV_PART_MAIN);
        lv_obj_align(stLbl, LV_ALIGN_TOP_LEFT, 0, 0);

        if (pct >= 0 && tone == StatusTone::Progress) {
            lv_obj_t *bar = lv_bar_create(statusBox);
            lv_obj_set_size(bar, 220, 10);
            lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x374151), LV_PART_MAIN);
            lv_obj_set_style_bg_color(bar, kBarGreen, LV_PART_INDICATOR);
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
    lv_obj_set_style_bg_color(screen_, PaxxTheme::bg(true), LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "Printer", farm_back_fleet_cb, app, true);

    nameLbl_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(nameLbl_, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(nameLbl_, LV_ALIGN_TOP_LEFT, 24, 64);
    lv_label_set_text(nameLbl_, "Printer");

    statusLbl_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(statusLbl_, PaxxTheme::accent(), LV_PART_MAIN);
    lv_obj_align(statusLbl_, LV_ALIGN_TOP_RIGHT, -24, 68);
    lv_label_set_text(statusLbl_, "");

    taskLbl_ = lv_label_create(screen_);
    lv_obj_set_width(taskLbl_, 752);
    lv_label_set_long_mode(taskLbl_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(taskLbl_, PaxxTheme::muted(true), LV_PART_MAIN);
    lv_obj_align(taskLbl_, LV_ALIGN_TOP_LEFT, 24, 96);
    lv_label_set_text(taskLbl_, "No task");

    percentLbl_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(percentLbl_, &lv_font_montserrat_18, LV_PART_MAIN);
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
    lv_obj_set_style_bg_color(bar_, lv_color_hex(0x374151), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, kBarGreen, LV_PART_INDICATOR);
    lv_bar_set_range(bar_, 0, 100);
    lv_bar_set_value(bar_, 0, LV_ANIM_OFF);

    tempLbl_ = lv_label_create(screen_);
    lv_obj_align(tempLbl_, LV_ALIGN_TOP_LEFT, 24, 214);
    lv_label_set_text(tempLbl_, "Nozzle -- C    Bed -- C");

    metaLbl_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(metaLbl_, PaxxTheme::muted(true), LV_PART_MAIN);
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
    styleActionBtn(edit, PaxxTheme::surface(true), 230);
    lv_obj_align(edit, LV_ALIGN_TOP_LEFT, 24, 368);
    lv_obj_add_event_cb(edit, onEdit, LV_EVENT_CLICKED, this);
    centeredLabel(edit, "Connection");

    confirm_ = lv_obj_create(screen_);
    lv_obj_set_size(confirm_, 420, 180);
    lv_obj_center(confirm_);
    lv_obj_set_style_bg_color(confirm_, PaxxTheme::surface(true), LV_PART_MAIN);
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
    snprintf(meta, sizeof(meta), "%s   %s   %s%s%s",
             p.ip, p.serial[0] ? p.serial : "no serial",
             st.wifi[0] ? st.wifi : "",
             st.speedPct >= 0 ? "   Speed " : "",
             "");
    if (st.speedPct >= 0) {
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

void SetupScreen::create(CupboardApp *app, lv_obj_t *parent) {
    app_ = app;
    screen_ = lv_obj_create(parent);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_, 0, LV_PART_MAIN);
    paxx_style_form_screen(screen_);
    paxx_create_nav_bar(screen_, "Printer Setup", farm_back_fleet_cb, app, true);

    int y = 56;
    auto addField = [&](lv_obj_t **ta, const char *ph, PaxxKbMode mode) {
        *ta = lv_textarea_create(screen_);
        styleTa(*ta);
        lv_obj_align(*ta, LV_ALIGN_TOP_MID, 0, y);
        y += 48;
        lv_textarea_set_placeholder_text(*ta, ph);
        PaxxKeyboard::attach(*ta, mode);
    };

    addField(&nameTa_, "Printer name (e.g. A1 Mini AMS)", PaxxKbMode::Text);
    addField(&ipTa_, "IP address (e.g. 192.168.1.50)", PaxxKbMode::Number);
    addField(&codeTa_, "LAN access code", PaxxKbMode::Password);
    addField(&serialTa_, "Serial (optional — auto from printer)", PaxxKbMode::Text);

    hintLbl_ = lv_label_create(screen_);
    paxx_set_form_width(hintLbl_);
    lv_obj_align(hintLbl_, LV_ALIGN_TOP_MID, 0, y);
    y += 28;
    lv_obj_set_style_text_color(hintLbl_, PaxxTheme::muted(true), LV_PART_MAIN);
    lv_label_set_text(hintLbl_, "Access code is on the printer under Settings > WLAN. Cloud mode is fine — LAN Only is not required.");

    lv_obj_t *scanBtn = lv_button_create(screen_);
    paxx_set_form_width(scanBtn);
    lv_obj_set_height(scanBtn, 40);
    lv_obj_align(scanBtn, LV_ALIGN_TOP_MID, 0, y);
    y += 48;
    lv_obj_add_event_cb(scanBtn, [](lv_event_t *e) {
        auto *self = static_cast<SetupScreen *>(lv_event_get_user_data(e));
        self->app_->startDiscovery();
        self->refreshDiscoverList();
    }, LV_EVENT_CLICKED, this);
    lv_label_set_text(lv_label_create(scanBtn), LV_SYMBOL_REFRESH "  Discover on LAN");

    discoverList_ = lv_list_create(screen_);
    paxx_set_form_width(discoverList_);
    lv_obj_set_height(discoverList_, 90);
    lv_obj_align(discoverList_, LV_ALIGN_TOP_MID, 0, y);
    y += 100;

    lv_obj_t *save = lv_button_create(screen_);
    paxx_set_form_width(save);
    lv_obj_set_height(save, 44);
    lv_obj_align(save, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(save, [](lv_event_t *e) {
        static_cast<CupboardApp *>(lv_event_get_user_data(e))->savePrinterFromSetup();
    }, LV_EVENT_CLICKED, app);
    lv_label_set_text(lv_label_create(save), "Save printer");
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
    lv_obj_set_style_text_color(hintLbl_, PaxxTheme::muted(true), LV_PART_MAIN);

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
        lv_obj_set_style_bg_color(row, PaxxTheme::surface(true), LV_PART_MAIN);
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
        lv_label_set_text(h, p.ip[0] ? p.ip : "No IP");
        lv_obj_set_style_text_color(h, PaxxTheme::muted(true), LV_PART_MAIN);
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

    hintLbl_ = lv_label_create(screen_);
    paxx_set_form_width(hintLbl_);
    lv_obj_align(hintLbl_, LV_ALIGN_TOP_MID, 0, 70);
    lv_label_set_long_mode(hintLbl_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hintLbl_,
                      "PandaCupboard " PANDACUPBOARD_VERSION "\n"
                      "PandaTouch / K-Touch Bambu Lab cupboard dashboard.\n\n"
                      "Created by TechJeeper Designs\n\n"
                      "Connect printers with IP + LAN access code.\n"
                      "MQTT/TLS 8883 user bblp. SSDP discovery on UDP 1990.\n"
                      "Cupboard sorts by Device Status; active jobs with higher completion stay on top.");
}
