#include "robot_face.h"

#include <algorithm>
#include <cstring>

#include <esp_random.h>

#include "application.h"

namespace {

constexpr uint32_t kInk = 0x2D2440;
constexpr uint32_t kWhite = 0xFFFFFF;
constexpr uint32_t kMouthInside = 0xC8374B;
constexpr uint32_t kTongue = 0xFF9AA8;
constexpr uint32_t kBlush = 0xFF8FAB;
constexpr uint32_t kHeart = 0xFF4F8B;
constexpr uint32_t kTear = 0x5CC8FF;
constexpr uint32_t kAnger = 0xE5383B;

// Design space is 128x160; everything below is in design units.
constexpr float kDesignW = 128.0f;
constexpr float kDesignH = 160.0f;

constexpr float kEyeCx[2] = {36.0f, 92.0f};
constexpr float kEyeCy = 70.0f;
constexpr float kEyeD = 44.0f;
constexpr float kMouthCx = 64.0f;
constexpr float kMouthCy = 100.0f;
constexpr float kCheekCy = 102.0f;

constexpr int kTicksPerSecond = 10;
constexpr int kDemoTicksPerExpression = 25;

enum Expression {
    kSmile, kHappy, kLaugh, kLove, kWink, kSad, kCry, kAngry,
    kSurprised, kThinking, kConfused, kShy, kCool, kSleepy, kExpressionCount
};

enum EyeMode { kEyeOpen, kEyeHappy, kEyeClosed, kEyeHeart, kEyeSquint, kEyeGlasses };
enum BrowMode { kBrowNone, kBrowAngry, kBrowWorried, kBrowRaised };
enum MouthMode {
    kMouthCat, kMouthSmile, kMouthOpen, kMouthLaugh, kMouthFrown, kMouthWail,
    kMouthPout, kMouthO, kMouthWavy, kMouthSmirk, kMouthTongue
};

struct Pose {
    EyeMode eye[2];
    float eye_scale[2];
    uint32_t background;
    uint32_t iris;
    float pupil_dx;  // both pupils always move together, so the eyes never look cross-eyed
    float pupil_dy;
    BrowMode brow;
    MouthMode mouth;
    bool blush;
};

// Same order as enum Expression.
const Pose kPoses[kExpressionCount] = {
    {{kEyeOpen, kEyeOpen}, {1, 1}, 0xFFE6D2, 0x35B6E6, 0, 0, kBrowNone, kMouthCat, true},          // smile
    {{kEyeHappy, kEyeHappy}, {1, 1}, 0xFFF0B3, 0xFFB300, 0, 0, kBrowNone, kMouthOpen, true},       // happy
    {{kEyeSquint, kEyeSquint}, {1, 1}, 0xFFE3A0, 0xFFB300, 0, 0, kBrowNone, kMouthLaugh, true},    // laugh
    {{kEyeHeart, kEyeHeart}, {1, 1}, 0xFFD6E5, 0xFF4F8B, 0, 0, kBrowNone, kMouthCat, true},        // love
    {{kEyeOpen, kEyeHappy}, {1, 1}, 0xD6F4FF, 0x35B6E6, 0, 0, kBrowNone, kMouthTongue, true},      // wink
    {{kEyeOpen, kEyeOpen}, {1, 1}, 0xD6E6FF, 0x6C9BFF, 0, 2, kBrowWorried, kMouthFrown, false},    // sad
    {{kEyeClosed, kEyeClosed}, {1, 1}, 0xC6DAFF, 0x6C9BFF, 0, 0, kBrowWorried, kMouthWail, true},  // cry
    {{kEyeOpen, kEyeOpen}, {1, 1}, 0xFFD0CA, 0xFF5A4F, 0, 2, kBrowAngry, kMouthPout, true},        // angry
    {{kEyeOpen, kEyeOpen}, {1.1f, 1.1f}, 0xDFFFD2, 0x4CD964, 0, 0, kBrowRaised, kMouthO, false},   // surprised
    {{kEyeOpen, kEyeOpen}, {1, 1}, 0xE8DCFF, 0x9C6BFF, 6, -6, kBrowNone, kMouthWavy, false},       // thinking
    {{kEyeOpen, kEyeOpen}, {0.72f, 1.05f}, 0xE8DCFF, 0x9C6BFF, -4, 0, kBrowRaised, kMouthWavy, false},  // confused
    {{kEyeOpen, kEyeOpen}, {1, 1}, 0xFFD9E8, 0xFF6FA3, -7, 6, kBrowNone, kMouthSmile, true},       // shy
    {{kEyeGlasses, kEyeGlasses}, {1, 1}, 0xCFF3F2, 0x35B6E6, 0, 0, kBrowNone, kMouthSmirk, false}, // cool
    {{kEyeClosed, kEyeClosed}, {1, 1}, 0xDAD6EF, 0x8C86B8, 0, 0, kBrowNone, kMouthSmile, false},   // sleepy
};

struct NamedExpression {
    const char* name;
    Expression expression;
};

// The server's emotion names, plus the Indonesian words used by self.face.set_expression.
const NamedExpression kNames[] = {
    {"neutral", kSmile},      {"staticstate", kSmile}, {"happy", kHappy},       {"relaxed", kHappy},
    {"confident", kHappy},    {"delicious", kHappy},   {"laughing", kLaugh},    {"funny", kLaugh},
    {"silly", kLaugh},        {"loving", kLove},       {"kissy", kLove},        {"winking", kWink},
    {"sad", kSad},            {"crying", kCry},        {"angry", kAngry},       {"surprised", kSurprised},
    {"shocked", kSurprised},  {"thinking", kThinking}, {"confused", kConfused}, {"embarrassed", kShy},
    {"cool", kCool},          {"sleepy", kSleepy},

    {"senyum", kSmile},       {"senang", kHappy},      {"tertawa", kLaugh},     {"cinta", kLove},
    {"kedip", kWink},         {"sedih", kSad},         {"nangis", kCry},        {"marah", kAngry},
    {"kaget", kSurprised},    {"mikir", kThinking},    {"bingung", kConfused},  {"malu", kShy},
    {"keren", kCool},         {"ngantuk", kSleepy},
};

void StyleArc(lv_obj_t* arc, uint32_t color, int width) {
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
}

void SetVisible(lv_obj_t* obj, bool visible) {
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void SetBoxColor(lv_obj_t* obj, uint32_t color) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}

}  // namespace

int RobotFace::LookupExpression(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    for (const auto& entry : kNames) {
        if (strcmp(entry.name, name) == 0) {
            return entry.expression;
        }
    }
    return -1;
}

int RobotFace::S(float v) const { return std::max(1, static_cast<int>(v * scale_ + 0.5f)); }

lv_obj_t* RobotFace::CreateBox(uint32_t color, int radius) {
    lv_obj_t* obj = lv_obj_create(layer_);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* RobotFace::CreateLine(uint32_t color, int width) {
    lv_obj_t* line = lv_line_create(layer_);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
}

lv_obj_t* RobotFace::CreateArc(uint32_t color, int width) {
    lv_obj_t* arc = lv_arc_create(layer_);
    StyleArc(arc, color, width);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return arc;
}

void RobotFace::PlaceBox(lv_obj_t* obj, float cx, float cy, float w, float h) {
    const int pw = S(w);
    const int ph = S(h);
    lv_obj_set_size(obj, pw, ph);
    lv_obj_set_pos(obj, X(cx) - pw / 2, Y(cy) - ph / 2);
}

void RobotFace::PlaceArc(lv_obj_t* arc, float cx, float cy, float r, int start, int end) {
    // LVGL angles: 0 = 3 o'clock, increasing clockwise (90 = bottom).
    const int d = S(r * 2);
    lv_obj_set_size(arc, d, d);
    lv_obj_set_pos(arc, X(cx) - d / 2, Y(cy) - d / 2);
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_angles(arc, start, end);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_HIDDEN);
}

void RobotFace::SetLine(lv_obj_t* line, lv_point_precise_t* pts, float x0, float y0, float x1,
                        float y1) {
    pts[0] = {static_cast<lv_value_precise_t>(X(x0)), static_cast<lv_value_precise_t>(Y(y0))};
    pts[1] = {static_cast<lv_value_precise_t>(X(x1)), static_cast<lv_value_precise_t>(Y(y1))};
    lv_line_set_points(line, pts, 2);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_HIDDEN);
}

RobotFace::RobotFace(lv_obj_t* parent, int width, int height) {
    scale_ = std::min(width / kDesignW, height / kDesignH);
    offset_x_ = static_cast<int>((width - kDesignW * scale_) / 2);
    offset_y_ = static_cast<int>((height - kDesignH * scale_) / 2);

    layer_ = lv_obj_create(parent);
    lv_obj_set_style_border_width(layer_, 0, 0);
    lv_obj_set_style_pad_all(layer_, 0, 0);
    lv_obj_set_style_radius(layer_, 0, 0);
    lv_obj_set_style_bg_opa(layer_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(layer_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(layer_, width, height);
    lv_obj_set_pos(layer_, 0, 0);
    lv_obj_set_scrollbar_mode(layer_, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < 2; i++) {
        blush_[i] = CreateBox(kBlush, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(blush_[i], LV_OPA_60, 0);
        PlaceBox(blush_[i], i == 0 ? 17 : 111, kCheekCy, 20, 12);
    }
    for (int i = 0; i < kStripes; i++) {
        stripe_[i] = CreateLine(kHeart, S(2));
        const float cx = (i < kStripes / 2 ? 17 : 111) + (i % 3 - 1) * 5;
        SetLine(stripe_[i], stripe_points_[i], cx + 2, kCheekCy - 4, cx - 2, kCheekCy + 4);
        lv_obj_add_flag(stripe_[i], LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < 2; i++) {
        eye_[i] = CreateBox(kInk, LV_RADIUS_CIRCLE);
        iris_[i] = CreateBox(kInk, LV_RADIUS_CIRCLE);
        pupil_[i] = CreateBox(kInk, LV_RADIUS_CIRCLE);
        shine_[i] = CreateBox(kWhite, LV_RADIUS_CIRCLE);
        shine_small_[i] = CreateBox(kWhite, LV_RADIUS_CIRCLE);
        eye_arc_[i] = CreateArc(kInk, S(5));
        brow_[i] = CreateLine(kInk, S(4));
        glare_[i] = CreateLine(kWhite, S(3));
        for (int j = 0; j < 2; j++) {
            heart_lobe_[i][j] = CreateBox(kHeart, LV_RADIUS_CIRCLE);
            stroke_[i][j] = CreateLine(kInk, S(5));
        }
    }
    bridge_ = CreateLine(kInk, S(5));

    for (int i = 0; i < 2; i++) {
        mouth_arc_[i] = CreateArc(kInk, S(3));
    }
    mouth_line_ = CreateLine(kInk, S(3));

    mouth_clip_ = lv_obj_create(layer_);
    lv_obj_set_style_bg_opa(mouth_clip_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(mouth_clip_, 0, 0);
    lv_obj_set_style_radius(mouth_clip_, 0, 0);
    lv_obj_set_style_border_side(mouth_clip_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(mouth_clip_, S(3), 0);
    lv_obj_set_style_border_color(mouth_clip_, lv_color_hex(kInk), 0);
    lv_obj_set_style_border_post(mouth_clip_, true, 0);  // draw the flat top over the mouth
    lv_obj_remove_flag(mouth_clip_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(mouth_clip_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mouth_clip_, LV_OBJ_FLAG_HIDDEN);

    mouth_open_ = lv_obj_create(mouth_clip_);
    lv_obj_set_style_radius(mouth_open_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(mouth_open_, 0, 0);
    lv_obj_set_style_bg_color(mouth_open_, lv_color_hex(kMouthInside), 0);
    lv_obj_set_style_bg_opa(mouth_open_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mouth_open_, S(3), 0);
    lv_obj_set_style_border_color(mouth_open_, lv_color_hex(kInk), 0);
    lv_obj_remove_flag(mouth_open_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(mouth_open_, LV_OBJ_FLAG_CLICKABLE);

    tongue_ = lv_obj_create(mouth_clip_);
    lv_obj_set_style_radius(tongue_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(tongue_, 0, 0);
    lv_obj_set_style_bg_color(tongue_, lv_color_hex(kTongue), 0);
    lv_obj_set_style_bg_opa(tongue_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(tongue_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tongue_, LV_OBJ_FLAG_CLICKABLE);

    tongue_out_ = CreateBox(kTongue, S(5));
    lv_obj_add_flag(tongue_out_, LV_OBJ_FLAG_HIDDEN);

    mouth_box_ = CreateBox(kMouthInside, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(mouth_box_, S(3), 0);
    lv_obj_set_style_border_color(mouth_box_, lv_color_hex(kInk), 0);
    lv_obj_add_flag(mouth_box_, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < kTears; i++) {
        tear_[i] = CreateBox(kTear, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(tear_[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < kDots; i++) {
        dot_[i] = CreateBox(kInk, LV_RADIUS_CIRCLE);
        PlaceBox(dot_[i], 92 + i * 11, 30, 7, 7);
        lv_obj_add_flag(dot_[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 2; i++) {
        mark_[i] = CreateLine(kAnger, S(4));
    }
    bubble_ = CreateBox(kTear, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(bubble_, LV_OPA_50, 0);
    lv_obj_set_style_border_width(bubble_, S(2), 0);
    lv_obj_set_style_border_color(bubble_, lv_color_hex(kWhite), 0);
    lv_obj_add_flag(bubble_, LV_OBJ_FLAG_HIDDEN);

    Apply(kSmile);
    lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN);

    timer_ = lv_timer_create(TimerCallback, 1000 / kTicksPerSecond, this);
}

RobotFace::~RobotFace() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
    }
    if (layer_ != nullptr) {
        lv_obj_del(layer_);
    }
}

void RobotFace::SetExpression(int expression) {
    if (expression < 0 || expression >= kExpressionCount) {
        expression = kSmile;
    }
    requested_ = expression;
    if (hold_ticks_ == 0 && demo_step_ < 0) {
        Apply(requested_);
    }
}

void RobotFace::HoldExpression(int expression, int seconds) {
    demo_step_ = -1;
    hold_ticks_ = std::max(1, seconds) * kTicksPerSecond;
    Apply(expression);
}

void RobotFace::StartDemo() {
    hold_ticks_ = 0;
    demo_step_ = 0;
    demo_ticks_ = kDemoTicksPerExpression;
    Apply(0);
}

void RobotFace::Apply(int expression) {
    expression_ = expression;
    blinking_ = false;
    gaze_dx_ = 0;
    gaze_dy_ = 0;
    eye_dx_ = 0;
    eye_dy_ = 0;
    heart_pulse_ = 0;
    tick_ = 0;
    ResetBlinkCountdown();

    const auto& pose = kPoses[expression_];
    SetBoxColor(layer_, pose.background);
    for (int i = 0; i < 2; i++) {
        SetBoxColor(iris_[i], pose.iris);
        SetVisible(blush_[i], pose.blush);
    }
    for (int i = 0; i < kStripes; i++) {
        SetVisible(stripe_[i], expression_ == kShy);
    }
    ApplyEyes();
    ApplyMouth();
    ApplyExtras();
}

void RobotFace::ApplyHeart(int side, bool visible) {
    for (int j = 0; j < 2; j++) {
        SetVisible(heart_lobe_[side][j], visible);
    }
    if (!visible) {
        return;
    }
    const float cx = kEyeCx[side] + eye_dx_;
    const float cy = kEyeCy + eye_dy_;
    const float d = 25 + heart_pulse_ * 2;
    const float spread = 10 + heart_pulse_;
    for (int j = 0; j < 2; j++) {
        const float dir = j == 0 ? -1.0f : 1.0f;
        PlaceBox(heart_lobe_[side][j], cx + dir * spread, cy - 8, d, d);
        lv_obj_set_style_line_color(stroke_[side][j], lv_color_hex(kHeart), 0);
        lv_obj_set_style_line_width(stroke_[side][j], S(18), 0);
        SetLine(stroke_[side][j], stroke_points_[side][j], cx + dir * (spread + 3), cy - 2, cx,
                cy + 15 + heart_pulse_);
    }
}

void RobotFace::ApplySquint(int side, bool visible) {
    if (!visible) {
        return;
    }
    // ">" on the left eye and "<" on the right eye.
    const float cx = kEyeCx[side] + eye_dx_;
    const float cy = kEyeCy + eye_dy_;
    const float dir = side == 0 ? 1.0f : -1.0f;
    for (int j = 0; j < 2; j++) {
        lv_obj_set_style_line_color(stroke_[side][j], lv_color_hex(kInk), 0);
        lv_obj_set_style_line_width(stroke_[side][j], S(5), 0);
        SetLine(stroke_[side][j], stroke_points_[side][j], cx - dir * 11, cy + (j == 0 ? -10 : 10),
                cx + dir * 10, cy);
    }
}

void RobotFace::ApplyGlasses(bool visible) {
    SetVisible(bridge_, visible);
    for (int i = 0; i < 2; i++) {
        SetVisible(glare_[i], visible);
    }
    if (!visible) {
        return;
    }
    SetLine(bridge_, bridge_points_, kEyeCx[0] + 20, kEyeCy - 6, kEyeCx[1] - 20, kEyeCy - 6);
    for (int i = 0; i < 2; i++) {
        SetLine(glare_[i], glare_points_[i], kEyeCx[i] + 2, kEyeCy - 8, kEyeCx[i] - 8, kEyeCy + 6);
    }
}

void RobotFace::ApplyEyes() {
    const auto& pose = kPoses[expression_];
    for (int i = 0; i < 2; i++) {
        const float cx = kEyeCx[i] + eye_dx_;
        const float cy = kEyeCy + eye_dy_;
        const EyeMode wanted = pose.eye[i];
        const bool can_blink = wanted == kEyeOpen || wanted == kEyeHappy;
        const EyeMode mode = blinking_ && can_blink ? kEyeClosed : wanted;
        const bool open = mode == kEyeOpen;
        const bool glasses = mode == kEyeGlasses;

        SetVisible(eye_[i], open || glasses);
        SetVisible(iris_[i], open);
        SetVisible(pupil_[i], open);
        SetVisible(shine_[i], open);
        SetVisible(shine_small_[i], open);

        const bool strokes = mode == kEyeHeart || mode == kEyeSquint;
        for (int j = 0; j < 2; j++) {
            SetVisible(stroke_[i][j], strokes);
        }
        ApplyHeart(i, mode == kEyeHeart);
        ApplySquint(i, mode == kEyeSquint);

        if (open) {
            const float d = kEyeD * pose.eye_scale[i];
            lv_obj_set_style_radius(eye_[i], LV_RADIUS_CIRCLE, 0);
            PlaceBox(eye_[i], cx, cy, d, d);
            // Everything inside the eye moves together with the gaze.
            // The crescent sits low in the eye, so it has less room to move down than up.
            const float px = cx + std::clamp(pose.pupil_dx + gaze_dx_, -d * 0.1f, d * 0.1f);
            const float py = cy + std::clamp(pose.pupil_dy + gaze_dy_, -d * 0.16f, d * 0.05f);
            PlaceBox(iris_[i], px, py + d * 0.08f, d * 0.72f, d * 0.72f);
            PlaceBox(pupil_[i], px, py - d * 0.02f, d * 0.68f, d * 0.68f);
            // Same highlights in both eyes, so they never look cross-eyed.
            // They follow the gaze less than the pupil, which keeps them inside the eye.
            const float sx = cx + (px - cx) * 0.4f;
            const float sy = cy + (py - cy) * 0.4f;
            PlaceBox(shine_[i], sx + d * 0.14f, sy - d * 0.2f, d * 0.34f, d * 0.34f);
            PlaceBox(shine_small_[i], sx - d * 0.18f, sy + d * 0.16f, d * 0.15f, d * 0.15f);
        } else if (glasses) {
            lv_obj_set_style_radius(eye_[i], S(10), 0);
            PlaceBox(eye_[i], cx, cy, 44, 32);
        }

        if (mode == kEyeHappy) {
            PlaceArc(eye_arc_[i], cx, cy + 8, 15, 200, 340);  // upside-down U: ^ ^
        } else if (mode == kEyeClosed) {
            PlaceArc(eye_arc_[i], cx, cy - 9, 15, 20, 160);  // closed U
        } else {
            lv_obj_add_flag(eye_arc_[i], LV_OBJ_FLAG_HIDDEN);
        }

        const float dir = i == 0 ? 1.0f : -1.0f;  // from the outer edge to the middle of the face
        const float bx = kEyeCx[i] + eye_dx_;
        const float by = 40 + eye_dy_;
        switch (pose.brow) {
            case kBrowAngry:
                SetLine(brow_[i], brow_points_[i], bx - dir * 13, by - 5, bx + dir * 13, by + 5);
                break;
            case kBrowWorried:
                SetLine(brow_[i], brow_points_[i], bx - dir * 13, by + 3, bx + dir * 11, by - 6);
                break;
            case kBrowRaised:
                SetLine(brow_[i], brow_points_[i], bx - dir * 10, by - 7, bx + dir * 10, by - 7);
                break;
            default:
                lv_obj_add_flag(brow_[i], LV_OBJ_FLAG_HIDDEN);
                break;
        }
    }
    ApplyGlasses(pose.eye[0] == kEyeGlasses);
}

void RobotFace::ApplyMouth() {
    for (int i = 0; i < 2; i++) {
        lv_obj_add_flag(mouth_arc_[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mouth_clip_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mouth_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tongue_out_, LV_OBJ_FLAG_HIDDEN);

    const MouthMode mode = kPoses[expression_].mouth;
    const float cy = kMouthCy + eye_dy_;
    const bool unhappy = mode == kMouthFrown || mode == kMouthPout || mode == kMouthWail;

    if ((speaking_ && unhappy) || mode == kMouthWail || mode == kMouthO) {
        // Round mouth: talking while unhappy, wailing, or a surprised "o".
        float w = 18, h = 20;
        if (speaking_) {
            w = 16 + mouth_level_ * 2;
            h = 8 + mouth_level_ * 4;
        } else if (mode == kMouthWail) {
            w = 24;
            h = 18 + (tick_ % 2) * 3;
        }
        PlaceBox(mouth_box_, kMouthCx, cy + 4, w, h);
        lv_obj_remove_flag(mouth_box_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (speaking_ || mode == kMouthLaugh || mode == kMouthOpen) {
        // Open smile: flat top, round bottom, tongue when wide open.
        static const float kOpen[] = {9, 13, 17, 21};
        float h = 13;
        float width = 26;
        if (speaking_) {
            h = kOpen[mouth_level_];
        } else if (mode == kMouthLaugh) {
            h = 24.0f + (tick_ / 2 % 2) * 3;
            width = 36;
        }
        const int pw = S(width);
        const int ph = S(h);
        lv_obj_set_size(mouth_clip_, pw, ph);
        lv_obj_set_pos(mouth_clip_, X(kMouthCx) - pw / 2, Y(cy - 6));
        lv_obj_set_size(mouth_open_, pw, ph * 2);
        // Children start below the top border, so step back over it to keep the round bottom.
        const int border = S(3);
        lv_obj_set_pos(mouth_open_, 0, -ph - border);
        if (h >= 13) {
            const int tw = S(width * 0.55f);
            const int th = S(h * 0.5f);
            lv_obj_set_size(tongue_, tw, th);
            lv_obj_set_pos(tongue_, pw / 2 - tw / 2, ph - th - border * 2 - S(1));
            lv_obj_remove_flag(tongue_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(tongue_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(mouth_clip_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    switch (mode) {
        case kMouthCat:
            // Small "w" mouth
            PlaceArc(mouth_arc_[0], kMouthCx - 6, cy - 5, 7, 10, 170);
            PlaceArc(mouth_arc_[1], kMouthCx + 6, cy - 5, 7, 10, 170);
            break;
        case kMouthSmile:
            PlaceArc(mouth_arc_[0], kMouthCx, cy - 7, 10, 30, 150);
            break;
        case kMouthTongue:
            // Sticking the tongue out
            PlaceBox(tongue_out_, kMouthCx + 3, cy + 5, 11, 12);
            lv_obj_remove_flag(tongue_out_, LV_OBJ_FLAG_HIDDEN);
            PlaceArc(mouth_arc_[0], kMouthCx, cy - 9, 12, 30, 150);
            break;
        case kMouthFrown:
            PlaceArc(mouth_arc_[0], kMouthCx, cy + 13, 10, 215, 325);
            break;
        case kMouthPout:
            // Puffed-up pout
            PlaceArc(mouth_arc_[0], kMouthCx, cy + 9, 8, 200, 340);
            break;
        case kMouthWavy:
            PlaceArc(mouth_arc_[0], kMouthCx - 6, cy - 4, 6, 20, 160);
            PlaceArc(mouth_arc_[1], kMouthCx + 6, cy + 4, 6, 200, 340);
            break;
        default:  // kMouthSmirk: a little sideways smile
            SetLine(mouth_line_, mouth_line_points_, kMouthCx - 12, cy + 2, kMouthCx + 12, cy - 4);
            break;
    }
}

void RobotFace::ApplyExtras() {
    const float slow = tick_ % 40;
    for (int i = 0; i < kTears; i++) {
        bool visible = false;
        if (expression_ == kCry) {
            // Two tears under each eye, half a cycle apart.
            const float fall = (tick_ * 6 + (i % 2) * 28) % 56;
            const float x = i < 2 ? kEyeCx[0] - 4 : kEyeCx[1] + 4;
            PlaceBox(tear_[i], x, 84 + fall, 10, 14);
            visible = true;
        } else if (expression_ == kSad && i == 0) {
            // One big tear wells up at the corner of the eye, then rolls down.
            const float grow = std::min(slow / 15, 1.0f);
            const float fall = std::max(slow - 15, 0.0f) * 2.2f;
            PlaceBox(tear_[i], kEyeCx[0] - 12, 88 + fall, 5 + 4 * grow, 6 + 6 * grow);
            visible = true;
        }
        SetVisible(tear_[i], visible);
    }

    for (int i = 0; i < kDots; i++) {
        SetVisible(dot_[i], expression_ == kThinking && i < tick_ / 4 % (kDots + 1));
    }

    const bool angry = expression_ == kAngry;
    for (int i = 0; i < 2; i++) {
        if (angry) {
            const float r = 8 + (tick_ / 3 % 2) * 2;
            const float dir = i == 0 ? 1.0f : -1.0f;
            SetLine(mark_[i], mark_points_[i], 113 - r, 17 - dir * r, 113 + r, 17 + dir * r);
        } else {
            lv_obj_add_flag(mark_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (expression_ == kSleepy) {
        // A bubble that grows and shrinks with each breath.
        const int phase = tick_ % 30;
        const float d = 8 + (phase < 15 ? phase : 30 - phase) * 0.9f;
        PlaceBox(bubble_, 80, 94 - d / 2, d, d);
        lv_obj_remove_flag(bubble_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bubble_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RobotFace::Animate() {
    tick_++;
    switch (expression_) {
        case kSmile:
        case kHappy:
        case kShy:
            // A gentle bob, so the face feels alive.
            if (tick_ % 10 == 0) {
                eye_dy_ = tick_ / 10 % 2;
                ApplyEyes();
                if (!speaking_) {
                    ApplyMouth();
                }
            }
            break;
        case kCry:
            if (!speaking_) {
                ApplyMouth();
            }
            break;
        case kAngry:
            eye_dx_ = (tick_ % 2) * 2 - 1;  // trembling with a pout
            ApplyEyes();
            break;
        case kLaugh:
            eye_dy_ = (tick_ / 2 % 2) * 3;
            ApplyEyes();
            if (!speaking_) {
                ApplyMouth();
            }
            break;
        case kSurprised:
            if (tick_ < 6) {
                eye_dy_ = tick_ % 2 == 0 ? 0 : -3;  // a little jump
                ApplyEyes();
            }
            break;
        case kLove:
            heart_pulse_ = tick_ / 4 % 2;  // heartbeat
            ApplyEyes();
            break;
        default:
            break;
    }
    ApplyExtras();
}

void RobotFace::UpdateMouth() {
    const bool speaking = Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking;
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

void RobotFace::UpdateGaze() {
    // Only the default face looks around; both pupils move together.
    if (expression_ != kSmile || --gaze_countdown_ > 0) {
        return;
    }
    static const int kGaze[][2] = {{0, 0}, {0, 0}, {0, 0}, {-5, 0}, {5, 0}, {0, -4}, {-4, -3}, {4, -3}};
    const auto& g = kGaze[esp_random() % (sizeof(kGaze) / sizeof(kGaze[0]))];
    gaze_dx_ = g[0];
    gaze_dy_ = g[1];
    gaze_countdown_ = 20 + static_cast<int>(esp_random() % 40);  // 2 - 6 seconds
    ApplyEyes();
}

void RobotFace::Show() { lv_obj_remove_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

void RobotFace::Hide() { lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

bool RobotFace::IsVisible() const { return !lv_obj_has_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

void RobotFace::ResetBlinkCountdown() {
    // Next blink in 2.5 - 5.5 seconds
    blink_countdown_ = 25 + static_cast<int>(esp_random() % 30);
}

void RobotFace::TimerCallback(lv_timer_t* timer) {
    // Runs inside the LVGL task, which already holds the LVGL lock.
    auto self = static_cast<RobotFace*>(lv_timer_get_user_data(timer));
    if (!self->IsVisible()) {
        return;
    }

    if (self->demo_step_ >= 0 && --self->demo_ticks_ <= 0) {
        self->demo_ticks_ = kDemoTicksPerExpression;
        if (++self->demo_step_ < kExpressionCount) {
            self->Apply(self->demo_step_);
        } else {
            self->demo_step_ = -1;
            self->Apply(self->requested_);
        }
    }
    if (self->hold_ticks_ > 0 && --self->hold_ticks_ == 0) {
        self->Apply(self->requested_);
    }

    self->UpdateMouth();
    self->Animate();

    if (--self->blink_countdown_ > 0) {
        if (!self->blinking_) {
            self->UpdateGaze();
        }
        return;
    }
    if (self->blinking_) {
        self->blinking_ = false;
        self->ResetBlinkCountdown();
    } else {
        self->blinking_ = true;
        self->blink_countdown_ = 2;  // keep the eyes closed for ~200 ms
    }
    self->ApplyEyes();
}
