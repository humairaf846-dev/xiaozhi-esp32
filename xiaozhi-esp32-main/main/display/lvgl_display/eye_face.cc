#include "eye_face.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>

#include <esp_random.h>

#include "application.h"

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
    kEyeExpressionCount,
};

enum EyeKind { kEyeKindRect, kEyeKindArc, kEyeKindClosed };
enum LidKind { kLidNone, kLidAngry, kLidSad };
enum MouthKind { kMouthFlat, kMouthSmile, kMouthFrown, kMouthO, kMouthSmall };

struct EyeShape {
    EyeKind kind;
    float width_scale;
    float height_scale;
    int dx;  // in 1/16 of the base eye height
    int dy;  // in 1/16 of the base eye height
    bool circle;
    LidKind lid;
};

struct EyeExpressionDef {
    EyeShape eye[2];  // [0] = left eye, [1] = right eye
    MouthKind mouth;  // mouth shape while not speaking
    bool blush;       // show pink cheeks (color screens only)
};

// Pink used for the cheeks and the tongue.
constexpr uint32_t kPinkColor = 0xFF6F9F;

const EyeExpressionDef kEyeExpressions[kEyeExpressionCount] = {
    // kEyeNeutral (friendly default: always smiling while serving)
    {{{kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidNone},
      {kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidNone}},
     kMouthSmile, true},
    // kEyeHappy
    {{{kEyeKindArc, 1.0f, 1.0f, 0, 0, false, kLidNone},
      {kEyeKindArc, 1.0f, 1.0f, 0, 0, false, kLidNone}},
     kMouthSmile, true},
    // kEyeWink
    {{{kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidNone},
      {kEyeKindArc, 1.0f, 1.0f, 0, 0, false, kLidNone}},
     kMouthSmile, true},
    // kEyeSad
    {{{kEyeKindRect, 1.0f, 0.9f, 0, 1, false, kLidSad},
      {kEyeKindRect, 1.0f, 0.9f, 0, 1, false, kLidSad}},
     kMouthFrown, false},
    // kEyeAngry
    {{{kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidAngry},
      {kEyeKindRect, 1.0f, 1.0f, 0, 0, false, kLidAngry}},
     kMouthFrown, false},
    // kEyeSurprised (round eyes)
    {{{kEyeKindRect, 0.0f, 1.0f, 0, 0, true, kLidNone},
      {kEyeKindRect, 0.0f, 1.0f, 0, 0, true, kLidNone}},
     kMouthO, false},
    // kEyeThinking (looking up and to the side)
    {{{kEyeKindRect, 0.8f, 0.7f, 4, -3, false, kLidNone},
      {kEyeKindRect, 0.8f, 0.7f, 4, -3, false, kLidNone}},
     kMouthSmall, false},
    // kEyeConfused
    {{{kEyeKindRect, 0.9f, 0.55f, 0, 2, false, kLidNone},
      {kEyeKindRect, 1.0f, 1.05f, 0, 0, false, kLidNone}},
     kMouthSmall, false},
    // kEyeEmbarrassed (looking down)
    {{{kEyeKindRect, 0.9f, 0.6f, -2, 4, false, kLidNone},
      {kEyeKindRect, 0.9f, 0.6f, -2, 4, false, kLidNone}},
     kMouthSmile, true},
    // kEyeCool (half-closed)
    {{{kEyeKindRect, 1.0f, 0.5f, 0, 4, false, kLidNone},
      {kEyeKindRect, 1.0f, 0.5f, 0, 4, false, kLidNone}},
     kMouthSmile, true},
    // kEyeSleepy
    {{{kEyeKindClosed, 1.0f, 1.0f, 0, 3, false, kLidNone},
      {kEyeKindClosed, 1.0f, 1.0f, 0, 3, false, kLidNone}},
     kMouthSmall, true},
};

struct EmotionToEye {
    const char* name;
    EyeExpression expression;
};

const EmotionToEye kEmotionToEye[] = {
    {"neutral", kEyeNeutral},     {"staticstate", kEyeNeutral}, {"happy", kEyeHappy},
    {"laughing", kEyeHappy},      {"funny", kEyeHappy},         {"loving", kEyeHappy},
    {"delicious", kEyeHappy},     {"kissy", kEyeHappy},         {"silly", kEyeHappy},
    {"relaxed", kEyeHappy},       {"confident", kEyeHappy},     {"winking", kEyeWink},
    {"sad", kEyeSad},             {"crying", kEyeSad},          {"angry", kEyeAngry},
    {"surprised", kEyeSurprised}, {"shocked", kEyeSurprised},   {"thinking", kEyeThinking},
    {"confused", kEyeConfused},   {"embarrassed", kEyeEmbarrassed},
    {"cool", kEyeCool},           {"sleepy", kEyeSleepy},
};

}  // namespace

int EyeFace::LookupExpression(const char* emotion) {
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

EyeFace::EyeFace(lv_obj_t* parent, int width, int height, lv_color_t background,
                 lv_color_t foreground)
    : width_(width), height_(height) {
    if (height_ <= 32) {
        // Tiny 128x32 OLED
        base_w_ = 28;
        base_h_ = 22;
        gap_ = 20;
        radius_ = 8;
        arc_width_ = 4;
    } else {
        base_w_ = width_ * 3 / 10;
        gap_ = width_ * 14 / 100;
        base_h_ = std::min(height_ * 62 / 100, base_w_ * 13 / 10);
        radius_ = std::max(4, base_w_ * 2 / 5);  // softer, rounder eyes
        arc_width_ = std::max(4, base_w_ / 7);
    }

    // Screens tall enough (e.g. 128x160 LCD) get a mouth below the eyes.
    has_mouth_ = height_ >= 96;
    if (has_mouth_) {
        base_h_ = std::min(base_h_, height_ * 36 / 100);
        eye_cy_ = height_ * 38 / 100;
        mouth_cy_ = height_ * 73 / 100;
    } else {
        eye_cy_ = height_ / 2;
        mouth_cy_ = height_;
    }

    layer_ = lv_obj_create(parent);
    lv_obj_set_size(layer_, width_, height_);
    lv_obj_set_pos(layer_, 0, 0);
    lv_obj_set_style_pad_all(layer_, 0, 0);
    lv_obj_set_style_border_width(layer_, 0, 0);
    lv_obj_set_style_radius(layer_, 0, 0);
    lv_obj_set_style_bg_opa(layer_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(layer_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(layer_, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 2; i++) {
        eye_rect_[i] = lv_obj_create(layer_);
        lv_obj_set_style_border_width(eye_rect_[i], 0, 0);
        lv_obj_set_style_pad_all(eye_rect_[i], 0, 0);
        lv_obj_set_style_bg_opa(eye_rect_[i], LV_OPA_COVER, 0);
        lv_obj_remove_flag(eye_rect_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(eye_rect_[i], LV_OBJ_FLAG_CLICKABLE);

        eye_arc_[i] = lv_arc_create(layer_);
        lv_obj_remove_style(eye_arc_[i], nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(eye_arc_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(eye_arc_[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(eye_arc_[i], 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(eye_arc_[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_arc_opa(eye_arc_[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(eye_arc_[i], arc_width_, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(eye_arc_[i], true, LV_PART_INDICATOR);
        lv_arc_set_bg_angles(eye_arc_[i], 180, 360);
        lv_arc_set_angles(eye_arc_[i], 180, 360);
        lv_obj_add_flag(eye_arc_[i], LV_OBJ_FLAG_HIDDEN);

        // Sparkles: small background-colored dots inside the eye make it look shiny and cute.
        eye_shine_[i] = lv_obj_create(layer_);
        eye_shine_small_[i] = lv_obj_create(layer_);
        for (auto obj : {eye_shine_[i], eye_shine_small_[i]}) {
            lv_obj_set_style_border_width(obj, 0, 0);
            lv_obj_set_style_pad_all(obj, 0, 0);
            lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }

        // A thick line in the background color drawn over the eye acts as a slanted eyelid.
        eye_lid_[i] = lv_line_create(layer_);
        lv_obj_add_flag(eye_lid_[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (has_mouth_) {
        mouth_rect_ = lv_obj_create(layer_);
        lv_obj_set_style_border_width(mouth_rect_, 0, 0);
        lv_obj_set_style_pad_all(mouth_rect_, 0, 0);
        lv_obj_set_style_bg_opa(mouth_rect_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(mouth_rect_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(mouth_rect_, LV_OBJ_FLAG_CLICKABLE);

        mouth_arc_ = lv_arc_create(layer_);
        lv_obj_remove_style(mouth_arc_, nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(mouth_arc_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(mouth_arc_, 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(mouth_arc_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_arc_opa(mouth_arc_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(mouth_arc_, arc_width_, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(mouth_arc_, true, LV_PART_INDICATOR);
        lv_obj_add_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);

        // Background-colored box over the top half of the open mouth: flat top, round bottom.
        mouth_cover_ = lv_obj_create(layer_);
        tongue_ = lv_obj_create(layer_);
        for (auto obj : {mouth_cover_, tongue_}) {
            lv_obj_set_style_border_width(obj, 0, 0);
            lv_obj_set_style_pad_all(obj, 0, 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_radius(mouth_cover_, 0, 0);
        lv_obj_set_style_radius(tongue_, LV_RADIUS_CIRCLE, 0);
        // Keep the mouth parts behind the eyes so the cover never hides part of an eye.
        // Resulting order from the back: mouth_rect_, mouth_cover_, tongue_, then the eyes.
        lv_obj_move_background(tongue_);
        lv_obj_move_background(mouth_cover_);
        lv_obj_move_background(mouth_rect_);

        for (int i = 0; i < 2; i++) {
            blush_[i] = lv_obj_create(layer_);
            lv_obj_set_style_border_width(blush_[i], 0, 0);
            lv_obj_set_style_pad_all(blush_[i], 0, 0);
            lv_obj_set_style_radius(blush_[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(blush_[i], LV_OPA_70, 0);
            lv_obj_remove_flag(blush_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(blush_[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(blush_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    SetColors(background, foreground);
    Apply(false);
    ApplyMouth();
    ApplyBlush();
    lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN);

    blink_timer_ = lv_timer_create(BlinkTimerCallback, 100, this);
}

EyeFace::~EyeFace() {
    if (blink_timer_ != nullptr) {
        lv_timer_delete(blink_timer_);
    }
    if (layer_ != nullptr) {
        lv_obj_del(layer_);
    }
}

void EyeFace::SetColors(lv_color_t background, lv_color_t foreground) {
    lv_obj_set_style_bg_color(layer_, background, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(eye_rect_[i], foreground, 0);
        lv_obj_set_style_arc_color(eye_arc_[i], foreground, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(eye_lid_[i], background, 0);
        lv_obj_set_style_bg_color(eye_shine_[i], background, 0);
        lv_obj_set_style_bg_color(eye_shine_small_[i], background, 0);
    }
    if (has_mouth_) {
        lv_obj_set_style_bg_color(mouth_rect_, foreground, 0);
        lv_obj_set_style_arc_color(mouth_arc_, foreground, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(mouth_cover_, background, 0);
        lv_obj_set_style_bg_color(tongue_, lv_color_hex(kPinkColor), 0);
        for (int i = 0; i < 2; i++) {
            lv_obj_set_style_bg_color(blush_[i], lv_color_hex(kPinkColor), 0);
        }
    }
}

void EyeFace::SetExpression(int expression) {
    if (expression < 0 || expression >= kEyeExpressionCount) {
        expression = kEyeNeutral;
    }
    expression_ = expression;
    blinking_ = false;
    gaze_dx_ = 0;
    gaze_dy_ = 0;
    ResetBlinkCountdown();
    Apply(false);
    ApplyMouth();
    ApplyBlush();
}

void EyeFace::ApplyBlush() {
    if (!has_mouth_) {
        return;
    }
    const bool show = kEyeExpressions[expression_].blush;
    const int w = base_w_ * 6 / 10;
    const int h = std::max(4, base_h_ / 6);
    const int y = eye_cy_ + base_h_ / 2 + base_h_ / 4;
    for (int i = 0; i < 2; i++) {
        if (!show) {
            lv_obj_add_flag(blush_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        // Under each eye, shifted slightly towards the outside of the face.
        const int eye_cx = (i == 0) ? width_ / 2 - gap_ / 2 - base_w_ / 2
                                    : width_ / 2 + gap_ / 2 + base_w_ / 2;
        const int cx = eye_cx + ((i == 0) ? -base_w_ / 5 : base_w_ / 5);
        lv_obj_set_size(blush_[i], w, h);
        lv_obj_set_pos(blush_[i], cx - w / 2, y - h / 2);
        lv_obj_remove_flag(blush_[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void EyeFace::ApplyMouth() {
    if (!has_mouth_) {
        return;
    }
    const int mouth_w = base_w_ * 11 / 10;
    const int line_h = std::max(4, base_h_ / 7);

    lv_obj_add_flag(mouth_cover_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tongue_, LV_OBJ_FLAG_HIDDEN);

    if (speaking_) {
        // Talking: a smiling open mouth (flat top, round bottom, like a "D" turned on its
        // side) whose opening follows mouth_level_ (0..3).
        static const int kOpenPercent[] = {20, 45, 70, 95};
        const int max_open = base_h_ * 6 / 10;
        const int h = std::max(line_h, max_open * kOpenPercent[mouth_level_] / 100);
        const int w = mouth_w * (95 - mouth_level_ * 5) / 100;
        const int top = mouth_cy_ - h / 2;
        lv_obj_add_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mouth_rect_, LV_OBJ_FLAG_HIDDEN);
        // Draw an ellipse twice as tall and hide its upper half.
        lv_obj_set_style_radius(mouth_rect_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(mouth_rect_, w, h * 2);
        lv_obj_set_pos(mouth_rect_, width_ / 2 - w / 2, top - h);
        lv_obj_set_size(mouth_cover_, w + 2, h);
        lv_obj_set_pos(mouth_cover_, width_ / 2 - w / 2 - 1, top - h);
        lv_obj_remove_flag(mouth_cover_, LV_OBJ_FLAG_HIDDEN);
        if (h >= 12) {
            // A little pink tongue at the bottom of a wide open mouth.
            const int tw = w * 45 / 100;
            const int th = h * 45 / 100;
            lv_obj_set_size(tongue_, tw, th);
            lv_obj_set_pos(tongue_, width_ / 2 - tw / 2, top + h - th - h / 8);
            lv_obj_remove_flag(tongue_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    const auto mouth = kEyeExpressions[expression_].mouth;
    if (mouth == kMouthSmile || mouth == kMouthFrown) {
        // A wide arc: the lower part of a circle for a smile, the upper part for a frown.
        const int d = mouth_w * 3 / 2;
        const int r = d / 2;
        lv_obj_add_flag(mouth_rect_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mouth_arc_, d, d);
        if (mouth == kMouthSmile) {
            lv_arc_set_bg_angles(mouth_arc_, 30, 150);
            lv_arc_set_angles(mouth_arc_, 30, 150);
            // Visible part spans r*sin(30)..r below the centre; centre it on mouth_cy_.
            lv_obj_set_pos(mouth_arc_, width_ / 2 - r, mouth_cy_ - r - r * 3 / 4);
        } else {
            lv_arc_set_bg_angles(mouth_arc_, 210, 330);
            lv_arc_set_angles(mouth_arc_, 210, 330);
            lv_obj_set_pos(mouth_arc_, width_ / 2 - r, mouth_cy_ - r + r * 3 / 4);
        }
        return;
    }

    int w;
    int h;
    if (mouth == kMouthO) {
        w = h = base_h_ * 4 / 10;
    } else if (mouth == kMouthSmall) {
        w = mouth_w / 2;
        h = line_h;
    } else {  // kMouthFlat
        w = mouth_w * 3 / 4;
        h = line_h;
    }
    lv_obj_add_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mouth_rect_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(mouth_rect_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(mouth_rect_, w, h);
    lv_obj_set_pos(mouth_rect_, width_ / 2 - w / 2, mouth_cy_ - h / 2);
}

void EyeFace::UpdateMouth() {
    if (!has_mouth_) {
        return;
    }
    const bool speaking =
        Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking;
    if (speaking) {
        // Pick a new opening each tick, never the same twice, so the mouth keeps moving.
        int next = static_cast<int>(esp_random() % 3);
        if (next >= mouth_level_) {
            next++;
        }
        mouth_level_ = next;
        speaking_ = true;
        ApplyMouth();
    } else if (speaking_) {
        speaking_ = false;
        mouth_level_ = 0;
        ApplyMouth();
    }
}

void EyeFace::UpdateGaze() {
    // Only the friendly default face looks around; other expressions keep their pose.
    if (expression_ != kEyeNeutral || --gaze_countdown_ > 0) {
        return;
    }
    // Mostly look straight ahead, sometimes glance left, right or up.
    static const int kGaze[][2] = {{0, 0}, {0, 0}, {0, 0}, {-3, 0}, {3, 0}, {-2, -2}, {2, -2}};
    const auto& g = kGaze[esp_random() % (sizeof(kGaze) / sizeof(kGaze[0]))];
    gaze_dx_ = g[0];
    gaze_dy_ = g[1];
    gaze_countdown_ = 20 + static_cast<int>(esp_random() % 40);  // 2 - 6 seconds
    Apply(false);
}

void EyeFace::Show() { lv_obj_remove_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

void EyeFace::Hide() { lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

bool EyeFace::IsVisible() const { return !lv_obj_has_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

void EyeFace::ResetBlinkCountdown() {
    // Next blink in 2.5 - 5.5 seconds
    blink_countdown_ = 25 + static_cast<int>(esp_random() % 30);
}

void EyeFace::Apply(bool blink) {
    const auto& def = kEyeExpressions[expression_];
    const int cy = eye_cy_;
    const int lid_offset = height_;  // keeps lid line points positive; see lv_obj_set_pos below

    for (int i = 0; i < 2; i++) {
        const auto& shape = def.eye[i];
        const int base_cx = (i == 0) ? width_ / 2 - gap_ / 2 - base_w_ / 2
                                     : width_ / 2 + gap_ / 2 + base_w_ / 2;
        const int cx = base_cx + (shape.dx + gaze_dx_) * base_h_ / 16;
        const int ecy = cy + (shape.dy + gaze_dy_) * base_h_ / 16;

        lv_obj_add_flag(eye_lid_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye_shine_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye_shine_small_[i], LV_OBJ_FLAG_HIDDEN);

        if (shape.kind == kEyeKindArc) {
            lv_obj_add_flag(eye_rect_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_arc_[i], LV_OBJ_FLAG_HIDDEN);
            const int d = base_w_;
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
            w = base_w_;
            h = std::max(3, base_h_ / 8);
            r = 1;
        } else if (shape.circle) {
            h = static_cast<int>(base_h_ * shape.height_scale);
            w = std::min(h, base_w_ * 12 / 10);
            h = w;
            r = LV_RADIUS_CIRCLE;
        } else {
            w = static_cast<int>(base_w_ * shape.width_scale);
            h = static_cast<int>(base_h_ * shape.height_scale);
            r = std::min(radius_, h / 2);
        }
        if (blink && shape.kind == kEyeKindRect) {
            h = std::max(2, base_h_ / 10);
            r = 1;
        }
        const int x0 = cx - w / 2;
        const int y0 = ecy - h / 2;
        lv_obj_set_style_radius(eye_rect_[i], r, 0);
        lv_obj_set_size(eye_rect_[i], w, h);
        lv_obj_set_pos(eye_rect_[i], x0, y0);

        if (!blink && shape.kind == kEyeKindRect && h >= 10) {
            // Big sparkle in the upper right corner of both eyes (same side, so the eyes
            // look in the same direction), a small one diagonally below it.
            const int s = std::max(3, std::min(w, h) / 4);
            const int sx = x0 + w - w / 6 - s;
            const int sy = y0 + h / 6;
            lv_obj_set_size(eye_shine_[i], s, s);
            lv_obj_set_pos(eye_shine_[i], sx, sy);
            lv_obj_remove_flag(eye_shine_[i], LV_OBJ_FLAG_HIDDEN);
            const int s2 = s / 2;
            if (s2 >= 2 && h >= 20) {
                const int sx2 = sx - s2 - s2 / 2;
                lv_obj_set_size(eye_shine_small_[i], s2, s2);
                lv_obj_set_pos(eye_shine_small_[i], sx2, sy + s + s2 / 2);
                lv_obj_remove_flag(eye_shine_small_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (shape.lid != kLidNone && !blink) {
            const int x1 = x0 + w;
            const int outer_x = (i == 0) ? x0 - 3 : x1 + 3;
            const int inner_x = (i == 0) ? x1 + 3 : x0 - 3;
            const int thickness = std::max(4, h * 7 / 10);
            const int low_y = y0 + h / 2;
            const int high_y = y0 - 1;
            // The bottom edge of the lid runs from high_y to low_y; the line centre sits above.
            int ax = (shape.lid == kLidAngry) ? outer_x : inner_x;
            int bx = (shape.lid == kLidAngry) ? inner_x : outer_x;
            lid_points_[i][0] = {static_cast<lv_value_precise_t>(ax + lid_offset),
                                 static_cast<lv_value_precise_t>(high_y - thickness / 2 +
                                                                 lid_offset)};
            lid_points_[i][1] = {static_cast<lv_value_precise_t>(bx + lid_offset),
                                 static_cast<lv_value_precise_t>(low_y - thickness / 2 +
                                                                 lid_offset)};
            lv_obj_set_style_line_width(eye_lid_[i], thickness, 0);
            lv_line_set_points(eye_lid_[i], lid_points_[i], 2);
            lv_obj_set_pos(eye_lid_[i], -lid_offset, -lid_offset);
            lv_obj_move_foreground(eye_lid_[i]);
            lv_obj_remove_flag(eye_lid_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void EyeFace::BlinkTimerCallback(lv_timer_t* timer) {
    // Runs inside the LVGL task, which already holds the LVGL lock.
    auto self = static_cast<EyeFace*>(lv_timer_get_user_data(timer));
    if (!self->IsVisible()) {
        return;
    }
    self->UpdateMouth();
    if (--self->blink_countdown_ > 0) {
        if (!self->blinking_) {
            self->UpdateGaze();
        }
        return;
    }
    if (self->blinking_) {
        self->blinking_ = false;
        self->Apply(false);
        self->ResetBlinkCountdown();
    } else {
        self->blinking_ = true;
        self->Apply(true);
        self->blink_countdown_ = 1;  // keep the eyes closed for ~100 ms
    }
}
