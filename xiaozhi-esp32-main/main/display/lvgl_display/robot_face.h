#ifndef ROBOT_FACE_H
#define ROBOT_FACE_H

#include <lvgl.h>

// Full-screen kawaii robot face for color LCDs: two big sparkling eyes and one small mouth on
// a pastel background that changes with the expression. The mouth moves while the robot talks.
// Drawn with plain LVGL objects in a 128x160 design space scaled to the screen.
//
// All methods except the constructor/destructor must be called with the display lock held.
class RobotFace {
public:
    RobotFace(lv_obj_t* parent, int width, int height);
    ~RobotFace();

    // Maps a server emotion ("happy") or an Indonesian name ("marah") to an expression,
    // or returns -1 for an unknown name.
    static int LookupExpression(const char* name);

    // The expression from the conversation. It shows at once, or when a hold or demo ends.
    void SetExpression(int expression);
    // Shows an expression for a fixed time, ignoring SetExpression() until it ends.
    void HoldExpression(int expression, int seconds);
    // Shows every expression in turn, then returns to the conversation's expression.
    void StartDemo();

    void Show();
    void Hide();
    bool IsVisible() const;
    lv_obj_t* layer() const { return layer_; }

private:
    static constexpr int kTears = 4;
    static constexpr int kStripes = 6;
    static constexpr int kDots = 3;

    lv_obj_t* layer_ = nullptr;
    lv_obj_t* blush_[2] = {};
    lv_obj_t* stripe_[kStripes] = {};  // shy lines on the cheeks
    lv_point_precise_t stripe_points_[kStripes][2] = {};

    lv_obj_t* eye_[2] = {};
    lv_obj_t* iris_[2] = {};  // colored crescent at the bottom of the eye
    lv_obj_t* pupil_[2] = {};
    lv_obj_t* shine_[2] = {};
    lv_obj_t* shine_small_[2] = {};
    lv_obj_t* eye_arc_[2] = {};  // happy and closed eye curves
    lv_obj_t* brow_[2] = {};
    lv_point_precise_t brow_points_[2][2] = {};
    lv_obj_t* heart_lobe_[2][2] = {};
    lv_obj_t* stroke_[2][2] = {};  // lower part of a heart, or the two strokes of "> <"
    lv_point_precise_t stroke_points_[2][2][2] = {};
    lv_obj_t* glare_[2] = {};  // sunglasses
    lv_point_precise_t glare_points_[2][2] = {};
    lv_obj_t* bridge_ = nullptr;
    lv_point_precise_t bridge_points_[2] = {};

    lv_obj_t* mouth_arc_[2] = {};  // the second arc is only used for the cat mouth
    lv_obj_t* mouth_line_ = nullptr;
    lv_point_precise_t mouth_line_points_[2] = {};
    lv_obj_t* mouth_clip_ = nullptr;  // clips the open mouth to its lower half (flat top)
    lv_obj_t* mouth_open_ = nullptr;
    lv_obj_t* tongue_ = nullptr;
    lv_obj_t* mouth_box_ = nullptr;  // round mouth
    lv_obj_t* tongue_out_ = nullptr;

    lv_obj_t* tear_[kTears] = {};
    lv_obj_t* dot_[kDots] = {};  // "thinking" dots
    lv_obj_t* mark_[2] = {};     // anger mark
    lv_point_precise_t mark_points_[2][2] = {};
    lv_obj_t* bubble_ = nullptr;  // sleeping bubble

    lv_timer_t* timer_ = nullptr;

    float scale_;
    int offset_x_;
    int offset_y_;

    int requested_ = 0;   // what the conversation asked for
    int expression_ = 0;  // what is on screen
    int hold_ticks_ = 0;
    int demo_step_ = -1;
    int demo_ticks_ = 0;
    int tick_ = 0;
    int blink_countdown_ = 30;
    bool blinking_ = false;
    bool speaking_ = false;
    int mouth_level_ = 0;
    int gaze_dx_ = 0;
    int gaze_dy_ = 0;
    int gaze_countdown_ = 40;
    float eye_dx_ = 0;  // shake and bounce shared by the eyes and the mouth
    float eye_dy_ = 0;
    float heart_pulse_ = 0;

    int X(float v) const { return offset_x_ + static_cast<int>(v * scale_ + 0.5f); }
    int Y(float v) const { return offset_y_ + static_cast<int>(v * scale_ + 0.5f); }
    int S(float v) const;  // scaled length, at least 1 pixel

    lv_obj_t* CreateBox(uint32_t color, int radius);
    lv_obj_t* CreateLine(uint32_t color, int width);
    lv_obj_t* CreateArc(uint32_t color, int width);
    void PlaceBox(lv_obj_t* obj, float cx, float cy, float w, float h);
    void PlaceArc(lv_obj_t* arc, float cx, float cy, float r, int start, int end);
    void SetLine(lv_obj_t* line, lv_point_precise_t* pts, float x0, float y0, float x1, float y1);

    void Apply(int expression);
    void ApplyEyes();
    void ApplyHeart(int side, bool visible);
    void ApplySquint(int side, bool visible);
    void ApplyGlasses(bool visible);
    void ApplyMouth();
    void ApplyExtras();
    void Animate();
    void UpdateMouth();
    void UpdateGaze();
    void ResetBlinkCountdown();
    static void TimerCallback(lv_timer_t* timer);
};

#endif  // ROBOT_FACE_H
