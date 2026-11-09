#include "cat_robot_face.h"

#include <algorithm>

#include <esp_random.h>

#include "application.h"

namespace {

// Palette
constexpr uint32_t kBlue = 0x1A8FE3;
constexpr uint32_t kWhite = 0xFFFFFF;
constexpr uint32_t kBlack = 0x111111;
constexpr uint32_t kRed = 0xE3242B;
constexpr uint32_t kMouthRed = 0xC81E32;
constexpr uint32_t kTongue = 0xFF8A8A;
constexpr uint32_t kBlush = 0xFF9EBB;

// Design space is 128x160; everything below is in design units.
constexpr float kDesignW = 128.0f;
constexpr float kDesignH = 160.0f;

constexpr float kEyeCx[2] = {49.0f, 79.0f};
constexpr float kEyeCy = 42.0f;
constexpr float kEyeW = 30.0f;
constexpr float kEyeH = 36.0f;
constexpr float kNoseCy = 64.0f;
constexpr float kNoseD = 14.0f;
constexpr float kOpenMouthTop = 88.0f;
constexpr float kOpenMouthW = 58.0f;

enum EyeMode { kEyeOpen, kEyeHappy, kEyeClosed };
enum MouthMode { kMouthSmile, kMouthSmall, kMouthFrown, kMouthO };

struct Pose {
    EyeMode eye[2];
    float pupil_dx;  // both pupils always move together, so the eyes never look cross-eyed
    float pupil_dy;
    float pupil_d;
    MouthMode mouth;
    bool blush;
    bool brows;
};

// Same order as the expressions in eye_face.cc (EyeFace::LookupExpression).
const Pose kPoses[] = {
    {{kEyeOpen, kEyeOpen}, 0, 3, 11, kMouthSmile, false, false},     // neutral
    {{kEyeHappy, kEyeHappy}, 0, 0, 11, kMouthSmile, true, false},    // happy
    {{kEyeOpen, kEyeHappy}, 0, 3, 11, kMouthSmile, true, false},     // wink
    {{kEyeOpen, kEyeOpen}, 0, 8, 11, kMouthFrown, false, false},     // sad
    {{kEyeOpen, kEyeOpen}, 0, 4, 9, kMouthFrown, false, true},       // angry
    {{kEyeOpen, kEyeOpen}, 0, 0, 7, kMouthO, false, false},          // surprised
    {{kEyeOpen, kEyeOpen}, 6, -7, 10, kMouthSmall, false, false},    // thinking
    {{kEyeOpen, kEyeOpen}, -6, -6, 10, kMouthSmall, false, false},   // confused
    {{kEyeOpen, kEyeOpen}, -5, 8, 10, kMouthSmall, true, false},     // embarrassed
    {{kEyeOpen, kEyeOpen}, 0, 3, 11, kMouthSmile, false, false},     // cool
    {{kEyeClosed, kEyeClosed}, 0, 0, 11, kMouthSmall, false, false}, // sleepy
};
constexpr int kPoseCount = sizeof(kPoses) / sizeof(kPoses[0]);

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

}  // namespace

int CatRobotFace::S(float v) const { return std::max(1, static_cast<int>(v * scale_ + 0.5f)); }

lv_obj_t* CatRobotFace::CreateBox(lv_obj_t* parent, uint32_t color, int radius) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* CatRobotFace::CreateLine(uint32_t color, int width) {
    lv_obj_t* line = lv_line_create(layer_);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_pos(line, 0, 0);
    return line;
}

lv_obj_t* CatRobotFace::CreateArc(uint32_t color, int width) {
    lv_obj_t* arc = lv_arc_create(layer_);
    StyleArc(arc, color, width);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return arc;
}

void CatRobotFace::PlaceBox(lv_obj_t* obj, float cx, float cy, float w, float h) {
    const int pw = S(w);
    const int ph = S(h);
    lv_obj_set_size(obj, pw, ph);
    lv_obj_set_pos(obj, X(cx) - pw / 2, Y(cy) - ph / 2);
}

void CatRobotFace::PlaceArc(lv_obj_t* arc, float cx, float cy, float r, int start, int end) {
    // LVGL angles: 0 = 3 o'clock, increasing clockwise (90 = bottom).
    const int d = S(r * 2);
    lv_obj_set_size(arc, d, d);
    lv_obj_set_pos(arc, X(cx) - d / 2, Y(cy) - d / 2);
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_angles(arc, start, end);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_HIDDEN);
}

void CatRobotFace::SetLine(lv_obj_t* line, lv_point_precise_t* pts, float x0, float y0, float x1,
                           float y1) {
    pts[0] = {static_cast<lv_value_precise_t>(X(x0)), static_cast<lv_value_precise_t>(Y(y0))};
    pts[1] = {static_cast<lv_value_precise_t>(X(x1)), static_cast<lv_value_precise_t>(Y(y1))};
    lv_line_set_points(line, pts, 2);
}

CatRobotFace::CatRobotFace(lv_obj_t* parent, int width, int height) {
    scale_ = std::min(width / kDesignW, height / kDesignH);
    offset_x_ = static_cast<int>((width - kDesignW * scale_) / 2);
    offset_y_ = static_cast<int>((height - kDesignH * scale_) / 2);

    // Blue head fills the whole screen.
    layer_ = CreateBox(parent, kBlue, 0);
    lv_obj_set_size(layer_, width, height);
    lv_obj_set_pos(layer_, 0, 0);
    lv_obj_set_scrollbar_mode(layer_, LV_SCROLLBAR_MODE_OFF);

    // White face
    face_ = CreateBox(layer_, kWhite, LV_RADIUS_CIRCLE);
    PlaceBox(face_, 64, 103, 120, 112);

    // Pink cheeks (only for some expressions)
    for (int i = 0; i < 2; i++) {
        blush_[i] = CreateBox(layer_, kBlush, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(blush_[i], LV_OPA_80, 0);
        PlaceBox(blush_[i], i == 0 ? 30 : 98, 74, 15, 8);
        lv_obj_add_flag(blush_[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Whiskers: three on each side
    static const float kWhiskerLeft[3][4] = {
        {14, 82, 42, 88}, {11, 94, 42, 94}, {14, 106, 42, 100}};
    for (int i = 0; i < kWhiskers; i++) {
        const auto& w = kWhiskerLeft[i % 3];
        whisker_[i] = CreateLine(kBlack, S(2));
        if (i < 3) {
            SetLine(whisker_[i], whisker_points_[i], w[0], w[1], w[2], w[3]);
        } else {
            SetLine(whisker_[i], whisker_points_[i], kDesignW - w[0], w[1], kDesignW - w[2], w[3]);
        }
    }

    philtrum_ = CreateLine(kBlack, S(2));

    // Mouth parts
    mouth_arc_ = CreateArc(kBlack, S(3));

    mouth_clip_ = lv_obj_create(layer_);
    lv_obj_set_style_bg_opa(mouth_clip_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(mouth_clip_, 0, 0);
    lv_obj_set_style_radius(mouth_clip_, 0, 0);
    lv_obj_set_style_border_side(mouth_clip_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(mouth_clip_, S(2.5f), 0);
    lv_obj_set_style_border_color(mouth_clip_, lv_color_hex(kBlack), 0);
    lv_obj_set_style_border_post(mouth_clip_, true, 0);  // draw the flat top over the mouth
    lv_obj_remove_flag(mouth_clip_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(mouth_clip_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mouth_clip_, LV_OBJ_FLAG_HIDDEN);

    mouth_open_ = CreateBox(mouth_clip_, kMouthRed, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(mouth_open_, S(2.5f), 0);
    lv_obj_set_style_border_color(mouth_open_, lv_color_hex(kBlack), 0);
    tongue_ = CreateBox(mouth_clip_, kTongue, LV_RADIUS_CIRCLE);

    mouth_o_ = CreateBox(layer_, kMouthRed, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(mouth_o_, S(2.5f), 0);
    lv_obj_set_style_border_color(mouth_o_, lv_color_hex(kBlack), 0);
    lv_obj_add_flag(mouth_o_, LV_OBJ_FLAG_HIDDEN);

    // Eyes
    for (int i = 0; i < 2; i++) {
        eye_white_[i] = CreateBox(layer_, kWhite, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(eye_white_[i], S(2), 0);
        lv_obj_set_style_border_color(eye_white_[i], lv_color_hex(kBlack), 0);
        pupil_[i] = CreateBox(layer_, kBlack, LV_RADIUS_CIRCLE);
        pupil_shine_[i] = CreateBox(layer_, kWhite, LV_RADIUS_CIRCLE);
        eye_arc_[i] = CreateArc(kBlack, S(3));
        brow_[i] = CreateLine(kBlack, S(3));
        lv_obj_add_flag(brow_[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Red nose with a small highlight, on top of everything
    nose_ = CreateBox(layer_, kRed, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(nose_, S(1.5f), 0);
    lv_obj_set_style_border_color(nose_, lv_color_hex(kBlack), 0);
    PlaceBox(nose_, 64, kNoseCy, kNoseD, kNoseD);
    nose_shine_ = CreateBox(layer_, kWhite, LV_RADIUS_CIRCLE);
    PlaceBox(nose_shine_, 61, kNoseCy - 3, 4, 4);

    SetExpression(0);
    lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN);

    timer_ = lv_timer_create(TimerCallback, 100, this);
}

CatRobotFace::~CatRobotFace() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
    }
    if (layer_ != nullptr) {
        lv_obj_del(layer_);
    }
}

void CatRobotFace::SetExpression(int expression) {
    if (expression < 0 || expression >= kPoseCount) {
        expression = 0;
    }
    expression_ = expression;
    blinking_ = false;
    gaze_dx_ = 0;
    gaze_dy_ = 0;
    ResetBlinkCountdown();
    ApplyEyes();
    ApplyMouth();
    for (int i = 0; i < 2; i++) {
        if (kPoses[expression_].blush) {
            lv_obj_remove_flag(blush_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(blush_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void CatRobotFace::ApplyEyes() {
    const auto& pose = kPoses[expression_];
    for (int i = 0; i < 2; i++) {
        const float cx = kEyeCx[i];
        PlaceBox(eye_white_[i], cx, kEyeCy, kEyeW, kEyeH);

        const EyeMode mode = blinking_ ? kEyeClosed : pose.eye[i];
        if (mode == kEyeOpen) {
            lv_obj_add_flag(eye_arc_[i], LV_OBJ_FLAG_HIDDEN);
            const float d = pose.pupil_d;
            // Keep the pupil inside the white of the eye.
            const float max_dx = kEyeW / 2 - d / 2 - 3;
            const float max_dy = kEyeH / 2 - d / 2 - 3;
            const float dx = std::clamp(pose.pupil_dx + gaze_dx_, -max_dx, max_dx);
            const float dy = std::clamp(pose.pupil_dy + gaze_dy_, -max_dy, max_dy);
            const float px = cx + dx;
            const float py = kEyeCy + dy;
            PlaceBox(pupil_[i], px, py, d, d);
            // Same highlight position in both eyes: upper right of the pupil.
            const float sd = std::max(3.0f, d * 0.4f);
            PlaceBox(pupil_shine_[i], px + d * 0.18f, py - d * 0.18f, sd, sd);
            lv_obj_remove_flag(pupil_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(pupil_shine_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pupil_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pupil_shine_[i], LV_OBJ_FLAG_HIDDEN);
            if (mode == kEyeHappy) {
                PlaceArc(eye_arc_[i], cx, kEyeCy + 6, 8, 200, 340);  // upside-down U: ^ ^
            } else {
                PlaceArc(eye_arc_[i], cx, kEyeCy - 3, 8, 20, 160);  // closed U
            }
        }

        if (pose.brows && !blinking_) {
            // Angry eyebrows slanting down towards the middle
            if (i == 0) {
                SetLine(brow_[i], brow_points_[i], 36, 21, 59, 29);
            } else {
                SetLine(brow_[i], brow_points_[i], 92, 21, 69, 29);
            }
            lv_obj_remove_flag(brow_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(brow_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void CatRobotFace::ApplyMouth() {
    lv_obj_add_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mouth_clip_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mouth_o_, LV_OBJ_FLAG_HIDDEN);

    const float nose_bottom = kNoseCy + kNoseD / 2;
    float mouth_top;  // where the philtrum line ends

    if (speaking_) {
        // Big open smiling mouth: flat top, round red bottom, tongue when wide open.
        static const float kOpen[] = {10, 18, 26, 34};
        const float h = kOpen[mouth_level_];
        const int pw = S(kOpenMouthW);
        const int ph = S(h);
        lv_obj_set_size(mouth_clip_, pw, ph);
        lv_obj_set_pos(mouth_clip_, X(64) - pw / 2, Y(kOpenMouthTop));
        lv_obj_set_size(mouth_open_, pw, ph * 2);
        lv_obj_set_pos(mouth_open_, 0, -ph);
        if (h >= 18) {
            const int tw = S(kOpenMouthW * 0.5f);
            const int th = S(h * 0.5f);
            lv_obj_set_size(tongue_, tw, th);
            lv_obj_set_pos(tongue_, pw / 2 - tw / 2, ph - th - S(3));
            lv_obj_remove_flag(tongue_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(tongue_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(mouth_clip_, LV_OBJ_FLAG_HIDDEN);
        mouth_top = kOpenMouthTop;
    } else {
        switch (kPoses[expression_].mouth) {
            case kMouthSmile:
                PlaceArc(mouth_arc_, 64, 72, 36, 25, 155);
                mouth_top = 108;
                break;
            case kMouthSmall:
                PlaceArc(mouth_arc_, 64, 84, 16, 30, 150);
                mouth_top = 100;
                break;
            case kMouthFrown:
                PlaceArc(mouth_arc_, 64, 124, 24, 210, 330);
                mouth_top = 100;
                break;
            case kMouthO:
            default:
                PlaceBox(mouth_o_, 64, 100, 16, 18);
                lv_obj_remove_flag(mouth_o_, LV_OBJ_FLAG_HIDDEN);
                mouth_top = 91;
                break;
        }
    }
    SetLine(philtrum_, philtrum_points_, 64, nose_bottom, 64, mouth_top);
}

void CatRobotFace::UpdateMouth() {
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

void CatRobotFace::UpdateGaze() {
    // Only the default face looks around; both pupils move together.
    if (expression_ != 0 || --gaze_countdown_ > 0) {
        return;
    }
    static const float kGaze[][2] = {{0, 0}, {0, 0}, {0, 0}, {-5, 0}, {5, 0}, {0, -4}, {-4, -3},
                                     {4, -3}};
    const auto& g = kGaze[esp_random() % (sizeof(kGaze) / sizeof(kGaze[0]))];
    gaze_dx_ = static_cast<int>(g[0]);
    gaze_dy_ = static_cast<int>(g[1]);
    gaze_countdown_ = 20 + static_cast<int>(esp_random() % 40);  // 2 - 6 seconds
    ApplyEyes();
}

void CatRobotFace::Show() { lv_obj_remove_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

void CatRobotFace::Hide() { lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

bool CatRobotFace::IsVisible() const { return !lv_obj_has_flag(layer_, LV_OBJ_FLAG_HIDDEN); }

void CatRobotFace::ResetBlinkCountdown() {
    // Next blink in 2.5 - 5.5 seconds
    blink_countdown_ = 25 + static_cast<int>(esp_random() % 30);
}

void CatRobotFace::TimerCallback(lv_timer_t* timer) {
    // Runs inside the LVGL task, which already holds the LVGL lock.
    auto self = static_cast<CatRobotFace*>(lv_timer_get_user_data(timer));
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
        self->ResetBlinkCountdown();
    } else {
        self->blinking_ = true;
        self->blink_countdown_ = 2;  // keep the eyes closed for ~200 ms
    }
    self->ApplyEyes();
}
