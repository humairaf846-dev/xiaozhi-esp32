#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;

    // Full-screen robot eyes drawn in place of the emoji
    lv_obj_t* eyes_layer_ = nullptr;
    lv_obj_t* eye_rect_[2] = {nullptr, nullptr};
    lv_obj_t* eye_arc_[2] = {nullptr, nullptr};
    lv_obj_t* eye_lid_[2] = {nullptr, nullptr};
    lv_point_precise_t eye_lid_points_[2][2] = {};
    lv_timer_t* blink_timer_ = nullptr;
    int current_eye_expression_ = 0;
    int blink_countdown_ = 30;
    bool blinking_ = false;

    void SetupEyes();
    void ApplyEyeExpression(int expression, bool blink = false);
    static void BlinkTimerCallback(lv_timer_t* timer);

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void RemoveChatMessage(const char* role, const char* content) override;
    virtual bool SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    void SetPowerSaveMode(bool on) override;
};

#endif  // OLED_DISPLAY_H
