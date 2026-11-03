#include "oled_display.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <algorithm>
#include <cstring>
#include <string>

#include <esp_err.h>
#include <esp_random.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <material_symbols.h>
#include <noto_emoji.h>

#define TAG "OledDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_1);
LV_FONT_DECLARE(font_noto_emoji_30_1);

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_1);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_1);

    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Note: SetupUI() should be called by Application::Initialize(), not in constructor
    // to ensure lvgl objects are created after the display is fully initialized.
}

void OledDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called
    if (height_ == 64) {
        SetupUI_128x64();
    } else {
        SetupUI_128x32();
    }
    SetupEyes();
}

OledDisplay::~OledDisplay() {
    if (blink_timer_ != nullptr) {
        lv_timer_delete(blink_timer_);
    }
    if (eyes_layer_ != nullptr) {
        lv_obj_del(eyes_layer_);
    }
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }

    bool is_128x64_layout = (top_bar_ != nullptr);
    if (status_bar_ != nullptr && is_128x64_layout) {
        status_label_ = nullptr;
        notification_label_ = nullptr;
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        network_label_ = nullptr;
        mute_label_ = nullptr;
        battery_label_ = nullptr;
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        if (!is_128x64_layout) {
            status_label_ = nullptr;
            notification_label_ = nullptr;
            network_label_ = nullptr;
            mute_label_ = nullptr;
            battery_label_ = nullptr;
        }
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void OledDisplay::Unlock() { lvgl_port_unlock(); }

void OledDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    // Replace all newlines with spaces
    std::string content_str = content;
    std::replace(content_str.begin(), content_str.end(), '\n', ' ');

    lv_anim_delete(chat_message_label_, nullptr);
    if (content_right_ == nullptr) {
        lv_label_set_text(chat_message_label_, content_str.c_str());
    } else {
        if (content == nullptr || content[0] == '\0') {
            lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(chat_message_label_, content_str.c_str());
            lv_obj_remove_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void OledDisplay::RemoveChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr || content == nullptr) {
        return;
    }
    // OLED has one text slot and does not distinguish message roles.
    std::string content_str = content;
    std::replace(content_str.begin(), content_str.end(), '\n', ' ');
    if (content_str != lv_label_get_text(chat_message_label_)) {
        return;
    }
    lv_anim_delete(chat_message_label_, nullptr);
    lv_label_set_text(chat_message_label_, "");
    if (content_right_ != nullptr) {
        lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
    }
}

void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(content_, LV_FLEX_ALIGN_CENTER, 0);

    content_left_ = lv_obj_create(content_);
    lv_obj_set_size(content_left_, 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content_left_, 0, 0);
    lv_obj_set_style_border_width(content_left_, 0, 0);

    emotion_label_ = lv_label_create(content_left_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);
    lv_obj_set_style_pad_top(emotion_label_, 8, 0);

    content_right_ = lv_obj_create(content_);
    lv_obj_set_size(content_right_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content_right_, 0, 0);
    lv_obj_set_style_border_width(content_right_, 0, 0);
    lv_obj_set_flex_grow(content_right_, 1);
    lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);

    chat_message_label_ = lv_label_create(content_right_);
    lv_label_set_text(chat_message_label_, "");
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(chat_message_label_, width_ - 32);
    lv_obj_set_style_pad_top(chat_message_label_, 14, 0);

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

void OledDisplay::SetupUI_128x32() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_column(container_, 0, 0);

    /* Emotion label on the left side */
    content_ = lv_obj_create(container_);
    lv_obj_set_size(content_, 32, 32);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);

    /* Right side */
    side_bar_ = lv_obj_create(container_);
    lv_obj_set_size(side_bar_, width_ - 32, 32);
    lv_obj_set_flex_flow(side_bar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(side_bar_, 0, 0);
    lv_obj_set_style_border_width(side_bar_, 0, 0);
    lv_obj_set_style_radius(side_bar_, 0, 0);
    lv_obj_set_style_pad_row(side_bar_, 0, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(side_bar_);
    lv_obj_set_size(status_bar_, width_ - 32, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_obj_set_style_pad_left(status_label_, 2, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_pad_left(notification_label_, 2, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    chat_message_label_ = lv_label_create(side_bar_);
    lv_obj_set_size(chat_message_label_, width_ - 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(chat_message_label_, 2, 0);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(chat_message_label_, "");

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);
}

// ---------------------------------------------------------------------------
// Robot eyes
// ---------------------------------------------------------------------------

namespace {

enum EyeExpression {
    kEyeNeutral = 0,
    kEyeHappy,
    kEyeWink,
    kEyeSad,
    kEyeAngry,
    kEyeSurprised,
    kEyeThinking,
    kEyeConfused,
    kEyeEmbarrassed,
    kEyeCool,
    kEyeSleepy,
};

enum EyeKind { kEyeKindRect, kEyeKindArc, kEyeKindClosed };
enum LidKind { kLidNone, kLidAngry, kLidSad };

struct EyeShape {
    EyeKind kind;
    float width_scale;
    float height_scale;
    int dx;
    int dy;
    bool circle;
    LidKind lid;
};

struct EyeExpressionDef {
    EyeShape eye[2];  // [0] = left eye, [1] = right eye
};

// dx/dy are in units of 1/16 of the base eye height so they scale with the screen.
const EyeExpressionDef kEyeExpressions[] = {
    // kEyeNeutral
    {{{kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidNone},
      {kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidNone}}},
    // kEyeHappy
    {{{kEyeKindArc, 1.0f, 1.0f, 0, 0, false, kLidNone},
      {kEyeKindArc, 1.0f, 1.0f, 0, 0, false, kLidNone}}},
    // kEyeWink
    {{{kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidNone},
      {kEyeKindArc, 1.0f, 1.0f, 0, 0, false, kLidNone}}},
    // kEyeSad
    {{{kEyeKindRect, 1.0f, 0.9f, 0, 1, false, kLidSad},
      {kEyeKindRect, 1.0f, 0.9f, 0, 1, false, kLidSad}}},
    // kEyeAngry
    {{{kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidAngry},
      {kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidAngry}}},
    // kEyeSurprised
    {{{kEyeKindRect, 0.0f, 1.0f, 0, 0, true, kLidNone},
      {kEyeKindRect, 0.0f, 1.0f, 0, 0, true, kLidNone}}},
    // kEyeThinking (looking up and to the side)
    {{{kEyeKindRect, 0.8f, 0.7f, 4, -3, false, kLidNone},
      {kEyeKindRect, 0.8f, 0.7f, 4, -3, false, kLidNone}}},
    // kEyeConfused
    {{{kEyeKindRect, 0.9f, 0.55f, 0, 2, false, kLidNone},
      {kEyeKindRect, 1.0f, 1.05f, 0, 0, false, kLidNone}}},
    // kEyeEmbarrassed (looking down)
    {{{kEyeKindRect, 0.9f, 0.6f, -2, 4, false, kLidNone},
      {kEyeKindRect, 0.9f, 0.6f, -2, 4, false, kLidNone}}},
    // kEyeCool (half-closed, relaxed)
    {{{kEyeKindRect, 1.0f, 0.5f, 0, 4, false, kLidNone},
      {kEyeKindRect, 1.0f, 0.5f, 0, 4, false, kLidNone}}},
    // kEyeSleepy
    {{{kEyeKindClosed, 1.0f, 1.0f, 0, 3, false, kLidNone},
      {kEyeKindClosed, 1.0f, 1.0f, 0, 3, false, kLidNone}}},
};

struct EmotionToEye {
    const char* name;
    EyeExpression expression;
};

const EmotionToEye kEmotionToEye[] = {
    {"neutral", kEyeNeutral},         {"staticstate", kEyeNeutral},
    {"happy", kEyeHappy},             {"laughing", kEyeHappy},
    {"funny", kEyeHappy},             {"loving", kEyeHappy},
    {"delicious", kEyeHappy},         {"kissy", kEyeHappy},
    {"silly", kEyeHappy},             {"relaxed", kEyeHappy},
    {"confident", kEyeHappy},         {"winking", kEyeWink},
    {"sad", kEyeSad},                 {"crying", kEyeSad},
    {"angry", kEyeAngry},             {"surprised", kEyeSurprised},
    {"shocked", kEyeSurprised},       {"thinking", kEyeThinking},
    {"confused", kEyeConfused},       {"embarrassed", kEyeEmbarrassed},
    {"cool", kEyeCool},               {"sleepy", kEyeSleepy},
};

int LookupEyeExpression(const char* emotion) {
    if (emotion == nullptr) {
        return -1;
    }
    for (const auto& entry : kEmotionToEye) {
        if (strcmp(entry.name, emotion) == 0) {
            return entry.expression;
        }
    }
    return -1;
}

}  // namespace

void OledDisplay::SetupEyes() {
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    // Use the same "lit" and "off" colors as the rest of the OLED UI.
    lv_color_t bg_color = lv_obj_get_style_bg_color(screen, LV_PART_MAIN);
    lv_color_t fg_color = lv_obj_get_style_text_color(screen, LV_PART_MAIN);

    eyes_layer_ = lv_obj_create(screen);
    lv_obj_set_size(eyes_layer_, width_, height_);
    lv_obj_set_pos(eyes_layer_, 0, 0);
    lv_obj_set_style_pad_all(eyes_layer_, 0, 0);
    lv_obj_set_style_border_width(eyes_layer_, 0, 0);
    lv_obj_set_style_radius(eyes_layer_, 0, 0);
    lv_obj_set_style_bg_color(eyes_layer_, bg_color, 0);
    lv_obj_set_style_bg_opa(eyes_layer_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(eyes_layer_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(eyes_layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(eyes_layer_, LV_OBJ_FLAG_CLICKABLE);

    int arc_width = height_ >= 64 ? 6 : 4;
    for (int i = 0; i < 2; i++) {
        eye_rect_[i] = lv_obj_create(eyes_layer_);
        lv_obj_set_style_border_width(eye_rect_[i], 0, 0);
        lv_obj_set_style_pad_all(eye_rect_[i], 0, 0);
        lv_obj_set_style_bg_color(eye_rect_[i], fg_color, 0);
        lv_obj_set_style_bg_opa(eye_rect_[i], LV_OPA_COVER, 0);
        lv_obj_remove_flag(eye_rect_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(eye_rect_[i], LV_OBJ_FLAG_CLICKABLE);

        eye_arc_[i] = lv_arc_create(eyes_layer_);
        lv_obj_remove_style(eye_arc_[i], nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(eye_arc_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(eye_arc_[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(eye_arc_[i], 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(eye_arc_[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_arc_opa(eye_arc_[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(eye_arc_[i], arc_width, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(eye_arc_[i], fg_color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(eye_arc_[i], true, LV_PART_INDICATOR);
        lv_arc_set_bg_angles(eye_arc_[i], 180, 360);
        lv_arc_set_angles(eye_arc_[i], 180, 360);
        lv_obj_add_flag(eye_arc_[i], LV_OBJ_FLAG_HIDDEN);

        // A thick line in the background color, drawn over the eye, acts as a slanted eyelid.
        eye_lid_[i] = lv_line_create(eyes_layer_);
        lv_obj_set_style_line_color(eye_lid_[i], bg_color, 0);
        lv_obj_add_flag(eye_lid_[i], LV_OBJ_FLAG_HIDDEN);
    }

    ApplyEyeExpression(kEyeNeutral);
    // Hidden until the first eye emotion arrives, so the boot/status text stays readable.
    lv_obj_add_flag(eyes_layer_, LV_OBJ_FLAG_HIDDEN);
    // Keep the low battery warning above the eyes.
    if (low_battery_popup_ != nullptr) {
        lv_obj_move_foreground(low_battery_popup_);
    }

    blink_timer_ = lv_timer_create(BlinkTimerCallback, 100, this);
}

void OledDisplay::ApplyEyeExpression(int expression, bool blink) {
    if (eyes_layer_ == nullptr || expression < 0 ||
        expression >= static_cast<int>(sizeof(kEyeExpressions) / sizeof(kEyeExpressions[0]))) {
        return;
    }
    current_eye_expression_ = expression;
    const auto& def = kEyeExpressions[expression];

    const bool large = height_ >= 64;
    const int base_w = large ? 36 : 28;
    const int base_h = large ? 40 : 22;
    const int gap = large ? 24 : 20;
    const int radius = large ? 10 : 6;
    const int cy = height_ / 2;
    const int lid_offset = 20;  // keeps lid line points positive; see lv_obj_set_pos below

    for (int i = 0; i < 2; i++) {
        const auto& shape = def.eye[i];
        const int base_cx = (i == 0) ? width_ / 2 - gap / 2 - base_w / 2
                                     : width_ / 2 + gap / 2 + base_w / 2;
        const int cx = base_cx + shape.dx * base_h / 16;
        const int ecy = cy + shape.dy * base_h / 16;

        lv_obj_add_flag(eye_lid_[i], LV_OBJ_FLAG_HIDDEN);

        if (shape.kind == kEyeKindArc) {
            lv_obj_add_flag(eye_rect_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_arc_[i], LV_OBJ_FLAG_HIDDEN);
            const int d = base_w;
            lv_obj_set_size(eye_arc_[i], d, d);
            lv_obj_set_pos(eye_arc_[i], cx - d / 2, ecy - d / 4);
            continue;
        }

        lv_obj_add_flag(eye_arc_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(eye_rect_[i], LV_OBJ_FLAG_HIDDEN);

        int w;
        int h;
        int r;
        if (shape.kind == kEyeKindClosed) {
            w = base_w;
            h = large ? 4 : 3;
            r = 1;
        } else if (shape.circle) {
            h = static_cast<int>(base_h * shape.height_scale);
            w = h;
            r = LV_RADIUS_CIRCLE;
        } else {
            w = static_cast<int>(base_w * shape.width_scale);
            h = static_cast<int>(base_h * shape.height_scale);
            r = std::min(radius, h / 2);
        }
        if (blink && shape.kind == kEyeKindRect) {
            h = large ? 4 : 2;
            r = 1;
        }
        const int x0 = cx - w / 2;
        const int y0 = ecy - h / 2;
        lv_obj_set_style_radius(eye_rect_[i], r, 0);
        lv_obj_set_size(eye_rect_[i], w, h);
        lv_obj_set_pos(eye_rect_[i], x0, y0);

        if (shape.lid != kLidNone && !blink) {
            const int x1 = x0 + w;
            const int outer_x = (i == 0) ? x0 - 3 : x1 + 3;
            const int inner_x = (i == 0) ? x1 + 3 : x0 - 3;
            const int thickness = std::max(4, h * 7 / 10);
            const int low_y = y0 + h / 2;
            const int high_y = y0 - 1;
            // Bottom edge of the lid runs from high_y to low_y; the line's centre sits above it.
            int ax, ay, bx, by;
            if (shape.lid == kLidAngry) {
                ax = outer_x;
                ay = high_y;
                bx = inner_x;
                by = low_y;
            } else {
                ax = inner_x;
                ay = high_y;
                bx = outer_x;
                by = low_y;
            }
            eye_lid_points_[i][0] = {static_cast<lv_value_precise_t>(ax + lid_offset),
                                     static_cast<lv_value_precise_t>(ay - thickness / 2 +
                                                                     lid_offset)};
            eye_lid_points_[i][1] = {static_cast<lv_value_precise_t>(bx + lid_offset),
                                     static_cast<lv_value_precise_t>(by - thickness / 2 +
                                                                     lid_offset)};
            lv_obj_set_style_line_width(eye_lid_[i], thickness, 0);
            lv_line_set_points(eye_lid_[i], eye_lid_points_[i], 2);
            lv_obj_set_pos(eye_lid_[i], -lid_offset, -lid_offset);
            lv_obj_move_foreground(eye_lid_[i]);
            lv_obj_remove_flag(eye_lid_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void OledDisplay::BlinkTimerCallback(lv_timer_t* timer) {
    // Runs inside the LVGL task, which already holds the LVGL lock.
    auto self = static_cast<OledDisplay*>(lv_timer_get_user_data(timer));
    if (self->eyes_layer_ == nullptr || lv_obj_has_flag(self->eyes_layer_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (--self->blink_countdown_ > 0) {
        return;
    }
    if (self->blinking_) {
        self->blinking_ = false;
        self->ApplyEyeExpression(self->current_eye_expression_);
        // Next blink in 2.5 - 5.5 seconds
        self->blink_countdown_ = 25 + static_cast<int>(esp_random() % 30);
    } else {
        self->blinking_ = true;
        self->ApplyEyeExpression(self->current_eye_expression_, true);
        self->blink_countdown_ = 1;  // keep the eyes closed for ~100 ms
    }
}

bool OledDisplay::SetEmotion(const char* emotion) {
    int eye_expression = LookupEyeExpression(emotion);
    // Names that are not facial emotions (e.g. "wifi", "link", "warning", "download") are
    // status icons: show them with the normal UI so setup and error messages stay readable.
    if (eye_expression < 0 && emotion != nullptr && material_symbols_get_utf8(emotion) == nullptr) {
        // Unknown emotion name from the server: fall back to neutral eyes.
        eye_expression = kEyeNeutral;
    }
    if (eye_expression >= 0) {
        DisplayLockGuard lock(this);
        if (eyes_layer_ == nullptr) {
            return false;
        }
        blinking_ = false;
        blink_countdown_ = 25 + static_cast<int>(esp_random() % 30);
        ApplyEyeExpression(eye_expression);
        lv_obj_remove_flag(eyes_layer_, LV_OBJ_FLAG_HIDDEN);
        return true;
    }

    {
        DisplayLockGuard lock(this);
        if (eyes_layer_ != nullptr) {
            lv_obj_add_flag(eyes_layer_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const char* utf8 = noto_emoji_get_utf8(emotion);
    const lv_font_t* emotion_font = lvgl_theme->emoji_font()->font();
    if (utf8 == nullptr) {
        utf8 = material_symbols_get_utf8(emotion);
        emotion_font = lvgl_theme->large_icon_font()->font();
    }
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return false;
    }
    if (utf8 != nullptr) {
        lv_obj_set_style_text_font(emotion_label_, emotion_font, 0);
        lv_label_set_text(emotion_label_, utf8);
        return true;
    }
    // Neither font has a glyph for this name, so what is shown is the neutral placeholder and
    // not the emotion that was asked for.
    lv_obj_set_style_text_font(emotion_label_, lvgl_theme->emoji_font()->font(), 0);
    lv_label_set_text(emotion_label_, NOTO_EMOJI_NEUTRAL);
    return false;
}

void OledDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
}

void OledDisplay::SetPowerSaveMode(bool on) {
    if (panel_) {
        Settings settings("wifi", false);
        if (settings.GetBool("power_save_display_off", false)) {
            esp_lcd_panel_disp_on_off(panel_, !on);
        }
    }
    LvglDisplay::SetPowerSaveMode(on);
}
