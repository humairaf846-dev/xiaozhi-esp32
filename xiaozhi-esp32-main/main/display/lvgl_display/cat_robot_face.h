#ifndef CAT_ROBOT_FACE_H
#define CAT_ROBOT_FACE_H

#include <lvgl.h>

// Full-screen blue cat-robot face for color LCDs: blue head, white face, big round eyes
// with pupils, red nose, whiskers and a mouth that smiles at rest and opens while talking.
// Drawn with plain LVGL objects in a 128x160 design space scaled to the screen.
//
// Expression indices are the ones returned by EyeFace::LookupExpression().
// All methods except the constructor/destructor must be called with the display lock held.
class CatRobotFace {
public:
    CatRobotFace(lv_obj_t* parent, int width, int height);
    ~CatRobotFace();

    void SetExpression(int expression);
    void Show();
    void Hide();
    bool IsVisible() const;
    lv_obj_t* layer() const { return layer_; }

private:
    static constexpr int kWhiskers = 6;

    lv_obj_t* layer_ = nullptr;
    lv_obj_t* face_ = nullptr;
    lv_obj_t* whisker_[kWhiskers] = {};
    lv_point_precise_t whisker_points_[kWhiskers][2] = {};
    lv_obj_t* philtrum_ = nullptr;  // vertical line from the nose to the mouth
    lv_point_precise_t philtrum_points_[2] = {};
    lv_obj_t* blush_[2] = {};

    lv_obj_t* mouth_arc_ = nullptr;   // smile / frown / small smile
    lv_obj_t* mouth_clip_ = nullptr;  // clips the open mouth to its lower half (flat top)
    lv_obj_t* mouth_open_ = nullptr;  // red open mouth, child of mouth_clip_
    lv_obj_t* tongue_ = nullptr;      // child of mouth_clip_
    lv_obj_t* mouth_o_ = nullptr;     // small round "O" mouth

    lv_obj_t* eye_white_[2] = {};
    lv_obj_t* pupil_[2] = {};
    lv_obj_t* pupil_shine_[2] = {};
    lv_obj_t* eye_arc_[2] = {};  // closed / happy eye curves
    lv_obj_t* brow_[2] = {};     // angry eyebrows
    lv_point_precise_t brow_points_[2][2] = {};

    lv_obj_t* nose_ = nullptr;
    lv_obj_t* nose_shine_ = nullptr;

    lv_timer_t* timer_ = nullptr;

    float scale_;
    int offset_x_;
    int offset_y_;

    int expression_ = 0;
    int blink_countdown_ = 30;
    bool blinking_ = false;
    bool speaking_ = false;
    int mouth_level_ = 0;
    int gaze_dx_ = 0;
    int gaze_dy_ = 0;
    int gaze_countdown_ = 40;

    int X(float v) const { return offset_x_ + static_cast<int>(v * scale_ + 0.5f); }
    int Y(float v) const { return offset_y_ + static_cast<int>(v * scale_ + 0.5f); }
    int S(float v) const;  // scaled length, at least 1 pixel

    lv_obj_t* CreateBox(lv_obj_t* parent, uint32_t color, int radius);
    lv_obj_t* CreateLine(uint32_t color, int width);
    lv_obj_t* CreateArc(uint32_t color, int width);
    void PlaceBox(lv_obj_t* obj, float cx, float cy, float w, float h);
    void PlaceArc(lv_obj_t* arc, float cx, float cy, float r, int start, int end);
    void SetLine(lv_obj_t* line, lv_point_precise_t* pts, float x0, float y0, float x1, float y1);

    void ApplyEyes();
    void ApplyMouth();
    void UpdateMouth();
    void UpdateGaze();
    void ResetBlinkCountdown();
    static void TimerCallback(lv_timer_t* timer);
};

#endif  // CAT_ROBOT_FACE_H
