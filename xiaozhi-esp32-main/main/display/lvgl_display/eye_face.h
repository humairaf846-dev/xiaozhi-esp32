#ifndef EYE_FACE_H
#define EYE_FACE_H

#include <lvgl.h>

// Full-screen robot eyes drawn with plain LVGL objects (rounded rectangles, arcs and
// thick lines used as eyelids). Used by the OLED and LCD displays in place of emoji.
//
// All methods except the constructor/destructor must be called with the display lock held.
class EyeFace {
public:
    EyeFace(lv_obj_t* parent, int width, int height, lv_color_t background, lv_color_t foreground);
    ~EyeFace();

    // Returns the eye expression index for an emotion name, or -1 when the name is not a
    // facial emotion (e.g. a status icon such as "wifi", "link" or "warning").
    static int LookupExpression(const char* emotion);

    void SetExpression(int expression);
    void SetColors(lv_color_t background, lv_color_t foreground);
    void Show();
    void Hide();
    bool IsVisible() const;
    lv_obj_t* layer() const { return layer_; }

private:
    lv_obj_t* layer_ = nullptr;
    lv_obj_t* eye_rect_[2] = {nullptr, nullptr};
    lv_obj_t* eye_arc_[2] = {nullptr, nullptr};
    lv_obj_t* eye_lid_[2] = {nullptr, nullptr};
    lv_point_precise_t lid_points_[2][2] = {};
    lv_obj_t* mouth_rect_ = nullptr;
    lv_obj_t* mouth_arc_ = nullptr;
    lv_timer_t* blink_timer_ = nullptr;

    int width_;
    int height_;
    int base_w_;
    int base_h_;
    int gap_;
    int radius_;
    int arc_width_;
    bool has_mouth_ = false;  // only on screens tall enough for eyes plus a mouth
    int eye_cy_;
    int mouth_cy_;

    int expression_ = 0;
    int blink_countdown_ = 30;
    bool blinking_ = false;
    bool speaking_ = false;
    int mouth_level_ = 0;

    void Apply(bool blink);
    void ApplyMouth();
    void UpdateMouth();
    void ResetBlinkCountdown();
    static void BlinkTimerCallback(lv_timer_t* timer);
};

#endif  // EYE_FACE_H
