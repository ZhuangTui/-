// w11ui / core.hpp ------------------------------------------------------------
// 基础类型：几何、颜色、COM 智能指针、缓动曲线、补间动画、时间与字符串工具。
// 头文件库，无 cpp，所有符号 inline。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <combaseapi.h>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <algorithm>
#include <cstdint>

namespace w11ui {

// ---------------------------------------------------------------- 几何 ----
struct Point {
    float x = 0.f;
    float y = 0.f;
    constexpr Point() = default;
    constexpr Point(float x_, float y_) : x(x_), y(y_) {}
};

struct Size {
    float w = 0.f;
    float h = 0.f;
    constexpr Size() = default;
    constexpr Size(float w_, float h_) : w(w_), h(h_) {}
};

struct Rect {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    float right()  const { return x + w; }
    float bottom() const { return y + h; }
    Point center() const { return Point(x + w * 0.5f, y + h * 0.5f); }
    Size  size()   const { return Size(w, h); }

    Rect inset(float d) const { return Rect(x + d, y + d, w - d * 2.f, h - d * 2.f); }
    Rect inset(float dx, float dy) const { return Rect(x + dx, y + dy, w - dx * 2.f, h - dy * 2.f); }
    Rect offset(float dx, float dy) const { return Rect(x + dx, y + dy, w, h); }
    Rect clamped_at_least(float minw, float minh) const {
        return Rect(x, y, w < minw ? minw : w, h < minh ? minh : h);
    }
    bool contains(Point p) const {
        return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
    }
};

// ---------------------------------------------------------------- 颜色 ----
struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}

    uint32_t argb() const {
        auto ch = [](float v) -> uint32_t {
            int i = (int)std::lround(v * 255.f);
            if (i < 0) i = 0;
            if (i > 255) i = 255;
            return (uint32_t)i;
        };
        return (ch(a) << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
    }
    Color alpha(float v) const { return Color(r, g, b, v); }
    // 0xRRGGBB 十六进制
    static Color hex(uint32_t v, float a_ = 1.f) {
        return Color(((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f, (v & 0xFF) / 255.f, a_);
    }
};

inline Color mix(Color a, Color b, float t) {
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return Color(a.r + (b.r - a.r) * t,
                 a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t,
                 a.a + (b.a - a.a) * t);
}
inline Color lighten(Color c, float t) { return mix(c, Color(1.f, 1.f, 1.f, c.a), t); }
inline Color darken(Color c, float t)  { return mix(c, Color(0.f, 0.f, 0.f, c.a), t); }

// ------------------------------------------------------------- COM RAII ---
template <class T>
class Com {
public:
    Com() = default;
    explicit Com(T* p) : p_(p) {}
    Com(const Com& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Com& operator=(const Com& o) {
        if (this != &o) { reset(); p_ = o.p_; if (p_) p_->AddRef(); }
        return *this;
    }
    Com& operator=(Com&& o) noexcept {
        if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    ~Com() { reset(); }

    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T*   get() const { return p_; }
    T*   operator->() const { return p_; }
    T**  put() { reset(); return &p_; }
    void** put_void() { reset(); return reinterpret_cast<void**>(&p_); }
    explicit operator bool() const { return p_ != nullptr; }
private:
    T* p_ = nullptr;
};

// --------------------------------------------------------------- COM ----
// WIC / D2D 工厂都需要 COM 已初始化；这里做一次幂等初始化，窗口和离屏渲染共用。
inline void ensure_com() {
    static bool done = false;
    if (!done) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        done = true;
    }
}

// --------------------------------------------------------------- 时间 ----
inline double now_ms() {
    static double freq = 0.0;
    static LARGE_INTEGER start{};
    if (freq == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = (double)f.QuadPart;
        QueryPerformanceCounter(&start);
    }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)(c.QuadPart - start.QuadPart) * 1000.0 / freq;
}

// --------------------------------------------------------------- 缓动 ----
using Easing = float (*)(float);

inline float ease_linear(float t) { return t; }
inline float ease_out_cubic(float t) { float f = t - 1.f; return f * f * f + 1.f; }
inline float ease_in_out_cubic(float t) {
    if (t < 0.5f) return 4.f * t * t * t;
    float f = -2.f * t + 2.f;
    return 1.f - (f * f * f) * 0.5f;
}
inline float ease_out_quint(float t) { float f = 1.f - t; return 1.f - f * f * f * f * f; }
inline float ease_out_back(float t) {  // 微软开关的轻微回弹
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.f;
    float f = t - 1.f;
    return 1.f + c3 * f * f * f + c1 * f * f;
}

// 下拉框 / 弹层 / 块装盒共用的一套"拉帘"动画 —— 三处必须是同一组常数，
// 否则同一屏里两个东西开合节奏不一样，一眼就能看出来。
//   展开 320ms：cubic-bezier(0.2, 0.5, 0.55, 1)，起步冲得快，中后段连续减速；
//   收起 100ms：(1-t)^2，起步慢、末段快。
// 三次贝塞尔（和 CSS 同一套）：给定 (x1,y1,x2,y2)，返回 e(t)。
// 不用 1-(1-t)^3：那个曲线 t=0.65 就走完 96%，后半段肉眼是"嗖一下到顶然后不动"。
[[nodiscard]] inline float ease_bezier(float t, float x1, float y1, float x2, float y2) {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    auto bez = [](float u, float p1, float p2) {
        float v = 1.f - u;
        return 3.f * v * v * u * p1 + 3.f * v * u * u * p2 + u * u * u;
    };
    float lo = 0.f, hi = 1.f, u = t;
    for (int i = 0; i < 30; ++i) {          // 二分求 x(u)=t 的 u，30 次足够到 1e-9
        u = (lo + hi) * 0.5f;
        float x = bez(u, x1, x2);
        if (x < t) lo = u; else hi = u;
    }
    return bez((lo + hi) * 0.5f, y1, y2);
}
inline float ease_popup_open(float t) { return ease_bezier(t, 0.20f, 0.50f, 0.55f, 1.f); }
inline float ease_popup_close(float t) {
    float u = 1.f - t;
    return u * u;
}
inline constexpr double kPopupOpenMs  = 320.0;
inline constexpr double kPopupCloseMs = 100.0;

// ------------------------------------------------------------- 补间动画 ---
// 一个数值从 from 过渡到 to，按毫秒时长 + 缓动函数插值。
struct Tween {
    float  value = 0.f;
    float  from  = 0.f;
    float  to    = 0.f;
    double start = 0.0;
    double dur   = 0.0;
    Easing ease  = ease_out_cubic;
    bool   running = false;

    void jump(float v) { value = from = to = v; running = false; }

    // 目标相等时不做无谓的重启，避免鼠标快速抖动时动画“打嗝”。
    void go(float target, double now, double duration_ms, Easing e = ease_out_cubic) {
        if (duration_ms <= 0.0) { jump(target); return; }
        if (running && target == to) return;
        if (!running && target == value) { to = target; return; }
        from = value;
        to = target;
        start = now;
        dur = duration_ms;
        ease = e;
        running = true;
    }

    // 推进到 now 并返回当前值；动画结束后自动停在终点。
    float at(double now) {
        if (!running) return value;
        float t = dur <= 0.0 ? 1.f : (float)((now - start) / dur);
        if (t >= 1.f || t < 0.f) {
            value = to;
            running = false;
            return value;
        }
        value = from + (to - from) * ease(t);
        return value;
    }

    bool active(double now) { at(now); return running; }
};

// --------------------------------------------------------------- 字符串 ---
inline std::wstring to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), need);
    return out;
}
inline std::wstring to_wide(const std::wstring& s) { return s; }
inline std::wstring to_wide(const wchar_t* s) { return s ? std::wstring(s) : std::wstring(); }
inline std::wstring to_wide(const char* s) { return s ? to_wide(std::string(s)) : std::wstring(); }

// --------------------------------------------------------------- DPI ----
// 高分屏下不糊的前提：进程声明 DPI 感知。优先 Per-Monitor V2，老系统退回系统级。
inline void enable_dpi_awareness() {
    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        auto fn = (SetCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (fn) {
            HANDLE ctx = (HANDLE)(LONG_PTR)-4;  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            if (fn(ctx)) return;
        }
    }
    SetProcessDPIAware();
}

// --------------------------------------------------------------- 枚举 ----
enum class Theme { Auto, Light, Dark };
enum class Variant { Primary, Standard, Subtle };      // 按钮三种视觉层级
enum class Role { Primary, Secondary, Accent, Disabled };
enum class TextScale { Caption, Body, BodyStrong, Subtitle, Title, Display };
enum class HAlign { Start, Center, End };

inline float text_scale_size(TextScale s) {
    switch (s) {
        case TextScale::Caption:    return 12.f;
        case TextScale::Body:       return 14.f;
        case TextScale::BodyStrong: return 14.f;
        case TextScale::Subtitle:   return 20.f;
        case TextScale::Title:      return 28.f;
        case TextScale::Display:    return 34.f;
        default:                    return 14.f;
    }
}
inline int text_scale_weight(TextScale s) {
    switch (s) {
        case TextScale::BodyStrong:
        case TextScale::Subtitle:
        case TextScale::Title:
        case TextScale::Display: return 600;  // DWRITE_FONT_WEIGHT_SEMI_BOLD
        default:                 return 400;
    }
}

}  // namespace w11ui
