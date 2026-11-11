// w11ui / painter.hpp ---------------------------------------------------------
// Direct2D + DirectWrite 绘制层。所有几何都按「逻辑像素」(96 DPI) 表达，
// 实际像素缩放由渲染目标的 DPI 承担，所以高分屏下不会糊。
#pragma once

#include "core.hpp"
#include "theme.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <initializer_list>
#include <cstdio>

namespace w11ui {

enum class VAlign { Top, Middle, Bottom };

// ------------------------------------------------------- 全局 COM 工厂 ----
// 进程内共享一套工厂，窗口和离屏渲染都用它。
inline ID2D1Factory* d2d_factory() {
    static Com<ID2D1Factory> fac;
    if (!fac) {
        D2D1_FACTORY_OPTIONS o{};
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &o,
                          fac.put_void());
    }
    return fac.get();
}

inline IDWriteFactory* dwrite_factory() {
    static Com<IDWriteFactory> fac;
    if (!fac) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(fac.put_void()));
    }
    return fac.get();
}

inline IWICImagingFactory* wic_factory() {
    ensure_com();
    static Com<IWICImagingFactory> fac;
    if (!fac) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IWICImagingFactory, fac.put_void());
    }
    return fac.get();
}

// 系统字体探测：Win11 有 Segoe UI Variable，老系统退回 Segoe UI。
inline bool font_family_exists(const std::wstring& family) {
    IDWriteFactory* dw = dwrite_factory();
    if (!dw) return false;
    Com<IDWriteFontCollection> coll;
    if (FAILED(dw->GetSystemFontCollection(coll.put(), FALSE))) return false;
    UINT32 idx = 0;
    BOOL exists = FALSE;
    coll->FindFamilyName(family.c_str(), &idx, &exists);
    return exists != FALSE;
}

// 文本排版用的 UI 语言（决定断行/标点挤压规则）。库不写死语言：默认跟随系统，
// 应用要指定就 set_ui_locale(L"en-us")。BCP-47 标签，大小写不敏感，这里统一小写。
inline std::wstring& ui_locale() {
    static std::wstring s = [] {
        wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0) {
            std::wstring v(buf);
            for (auto& c : v)
                if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
            return v;
        }
        return std::wstring(L"zh-cn");
    }();
    return s;
}
inline void set_ui_locale(const std::wstring& tag) { ui_locale() = tag; }
inline void set_ui_locale(const wchar_t* tag) { if (tag) ui_locale() = tag; }

inline std::wstring pick_family(std::initializer_list<const wchar_t*> candidates,
                                const wchar_t* fallback) {
    static std::map<std::wstring, std::wstring> cache;
    for (const wchar_t* c : candidates) {
        std::wstring key(c);
        auto it = cache.find(key);
        if (it != cache.end()) {
            if (!it->second.empty()) return it->second;
            continue;
        }
        bool ok = font_family_exists(key);
        cache[key] = ok ? key : std::wstring();
        if (ok) return key;
    }
    return std::wstring(fallback);
}

// 字体族：默认是 Fluent 的回退链（Segoe UI Variable → Segoe UI），
// 但库不把字体写死——应用可以整套换掉（中文字体走 w11ui::set_fonts，空 = 用默认链）：
//     w11ui::Fonts f; f.body = L"Microsoft YaHei UI"; w11ui::set_fonts(f);
struct Fonts {
    std::wstring body;      // 正文
    std::wstring display;   // 大标题
    std::wstring icon;      // 图标字体（✓ ▸ ⌄ 这类 glyph）
};
inline Fonts& fonts() { static Fonts f; return f; }
inline void set_fonts(const Fonts& f) { fonts() = f; }

inline std::wstring body_family() {
    if (!fonts().body.empty()) return fonts().body;
    static std::wstring f = pick_family({L"Segoe UI Variable Text", L"Segoe UI"}, L"Segoe UI");
    return f;
}
inline std::wstring display_family() {
    if (!fonts().display.empty()) return fonts().display;
    static std::wstring f = pick_family({L"Segoe UI Variable Display", L"Segoe UI Variable Text",
                                         L"Segoe UI"}, L"Segoe UI");
    return f;
}
inline std::wstring icon_family() {
    if (!fonts().icon.empty()) return fonts().icon;
    static std::wstring f = pick_family({L"Segoe Fluent Icons", L"Segoe MDL2 Assets"},
                                        L"Segoe UI Symbol");
    return f;
}

inline D2D1_COLOR_F d2d(Color c) { return D2D1_COLOR_F{c.r, c.g, c.b, c.a}; }
inline D2D1_RECT_F  d2d(const Rect& r) { return D2D1_RECT_F{r.x, r.y, r.right(), r.bottom()}; }
inline D2D1_POINT_2F d2dp(Point p) { return D2D1_POINT_2F{p.x, p.y}; }

// ----------------------------------------------------------------- Painter --
class Painter {
public:
    bool init(ID2D1RenderTarget* rt, const Palette& pal, float dpi_scale, bool bitmap_target,
              bool premultiply = false) {
        rt_ = rt;
        pal_ = pal;
        scale_ = dpi_scale;
        premul_ = premultiply;
        if (!rt_) return false;
        if (bitmap_target) {
            // WIC 位图目标不支持 ClearType，必须降级为灰度抗锯齿，否则文字会有彩边。
            rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        } else {
            rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        }
        return true;
    }

    void set_palette(const Palette& p) { pal_ = p; }
    const Palette& palette() const { return pal_; }
    void set_scale(float s) { scale_ = s; }
    float scale() const { return scale_; }
    ID2D1RenderTarget* target() const { return rt_; }
    bool valid() const { return rt_ != nullptr; }

    void begin(Color clear_color) {
        rt_->BeginDraw();
        D2D1_MATRIX_3X2_F identity = D2D1_MATRIX_3X2_F{1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
        rt_->SetTransform(identity);
        rt_->Clear(d2d(clear_color));
    }
    // 不清屏的 begin：目标上已经有画面（比如每帧只在动区上重画一小块），
    // Clear 会把已经贴好的底板一起抹掉。
    void begin_keep() {
        rt_->BeginDraw();
        D2D1_MATRIX_3X2_F identity = D2D1_MATRIX_3X2_F{1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
        rt_->SetTransform(identity);
    }
    void clear_transparent() {
        rt_->Clear(D2D1_COLOR_F{0.f, 0.f, 0.f, 0.f});
    }
    HRESULT end() {
        HRESULT hr = rt_->EndDraw();
        return hr;
    }

    void push_clip(const Rect& r) { rt_->PushAxisAlignedClip(d2d(r), D2D1_ANTIALIAS_MODE_ALIASED); }
    void pop_clip() { rt_->PopAxisAlignedClip(); }

    // 竖向平移（逻辑像素）。展开动画里"内容贴着正在生长的那条边"就靠它：
    // 只挪画，不挪布局，所以命中测试（row_rect）仍然按最终位置算。
    void push_translate(float dx, float dy) {
        D2D1_MATRIX_3X2_F m{1.f, 0.f, 0.f, 1.f, dx, dy};
        rt_->SetTransform(m);
    }
    void pop_translate() {
        D2D1_MATRIX_3X2_F m{1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
        rt_->SetTransform(m);
    }

    // 绕 (cx,cy) 缩放：弹出/关闭动画里"85% → 100%"就靠它。
    // 只影响画，不影响布局和命中（bounds / hit_test 仍是最终位置）。
    void push_scale(float cx, float cy, float s) {
        D2D1_MATRIX_3X2_F m{s, 0.f, 0.f, s, cx * (1.f - s), cy * (1.f - s)};
        rt_->SetTransform(m);
    }
    void pop_scale() {
        D2D1_MATRIX_3X2_F m{1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
        rt_->SetTransform(m);
    }

    // --------------------------------------------------- 整层不透明度 --
    // 只给自己的颜色乘 alpha 是不行的：控件里的子控件（弹窗 body 里的输入框）用的是
    // 它自己的主题色，面板淡出去了它还实心。PushLayer 是对"整段绘制"统一做 alpha。
    // area 用逻辑坐标，要给足整屏 —— 超出 contentBounds 的像素不受这层影响。
    void push_opacity(float opacity, const Rect& area) {
        if (!rt_ || opacity >= 0.999f) return;
        if (!layer_) rt_->CreateLayer(layer_.put());
        if (!layer_) return;
        D2D1_LAYER_PARAMETERS lp{};
        lp.contentBounds = d2d(area);
        lp.opacity = opacity;
        lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;
        rt_->PushLayer(&lp, layer_.get());
        layer_open_ = true;
    }
    void pop_opacity() {
        if (!layer_open_) return;
        rt_->PopLayer();
        layer_open_ = false;
        layer_.reset();          // 逐帧重建，不留旧状态
    }

    // ------------------------------------------------------------- 画刷 --
    // 目标是 PREMULTIPLIED（分层窗口的 DIB）时，D2D 期望「画刷颜色已经乘过 alpha」，
    // 否则半透明的东西会发白发亮，圆角外的阴影就变成一块实色。
    ID2D1SolidColorBrush* brush(Color c) {
        if (premul_ && c.a < 0.999f) {
            c.r *= c.a;
            c.g *= c.a;
            c.b *= c.a;
        }
        uint32_t k = c.argb();
        auto it = brushes_.find(k);
        if (it != brushes_.end()) return it->second.get();
        Com<ID2D1SolidColorBrush> b;
        rt_->CreateSolidColorBrush(d2d(c), b.put());
        ID2D1SolidColorBrush* raw = b.get();
        brushes_[k] = b;
        return raw;
    }

    // ------------------------------------------------------------- 形状 --
    void fill_rect(const Rect& r, Color c) {
        if (r.w <= 0 || r.h <= 0) return;
        rt_->FillRectangle(d2d(r), brush(c));
    }

    void fill_round(const Rect& r, float radius, Color c) {
        if (r.w <= 0 || r.h <= 0) return;
        float lim = (r.w < r.h ? r.w : r.h) * 0.5f;
        if (radius <= 0.5f) {
            fill_rect(r, c);
            return;
        }
        // 半径封顶到短边一半：此时是胶囊形（两端半圆、中间直边），不是椭圆
        if (radius > lim) radius = lim;
        rt_->FillRoundedRectangle(D2D1_ROUNDED_RECT{d2d(r), radius, radius}, brush(c));
    }

    void stroke_round(const Rect& r, float radius, Color c, float width = 1.f) {
        if (r.w <= 0 || r.h <= 0) return;
        float lim = (r.w < r.h ? r.w : r.h) * 0.5f;
        if (radius <= 0.f) {
            D2D1_RECT_F rr = D2D1_RECT_F{r.x + width * 0.5f, r.y + width * 0.5f,
                                         r.right() - width * 0.5f, r.bottom() - width * 0.5f};
            rt_->DrawRectangle(rr, brush(c), width);
            return;
        }
        if (radius > lim) radius = lim;
        rt_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{d2d(r), radius, radius}, brush(c), width);
    }

    // 磨砂面（亚克力在纯自绘界面上的视觉近似）：纵向渐变玻璃面 + 一圈亮边。
    // 真模糊要拿"窗口背后的像素"，D2D 在 HDC 客户区里做不到，所以这里按 Win11 亚克力的
    // 观感拼：面不做实色，上亮下暗做出厚度，圆角原样保持。
    void fill_glass(const Rect& r, float radius, float alpha = 0.62f) {
        if (r.w <= 0 || r.h <= 0) return;
        const Palette& pal = palette();
        std::vector<std::pair<float, Color>> stops;
        if (pal.dark) {
            // 深色：只有"比底略亮"才像玻璃；底部压到比背景暗就会显脏
            stops.push_back({0.f, Color::hex(0x3A3A3A, 0.60f)});
            stops.push_back({1.f, Color::hex(0x2C2C2C, 0.54f)});
        } else {
            stops.push_back({0.f, Color::hex(0xFFFFFF, 0.82f)});
            stops.push_back({1.f, Color::hex(0xF6F6F6, 0.66f)});
        }
        fill_round_linear(r, radius, stops);
        // 边缘一圈亮线：亚克力最标志的"玻璃边"
        float rr = std::min(radius, std::min(r.w, r.h) * 0.5f);
        stroke_round(r.inset(0.5f), rr, Color(1.f, 1.f, 1.f, pal.dark ? 0.09f : 0.55f), 1.f);
        (void)alpha;
    }

    // 整窗 / 标题栏那一层的玻璃底（不是控件级磨砂）。
    // 很轻：浅色只提亮 2~3 个色阶、深色只把上部提亮一点，绝不整体压暗、也不发灰。
    // 控件级的 fill_glass 才做上亮下暗的"厚度"，这两个别混用。
    void fill_surface_glass(const Rect& r, bool dark) {
        if (r.w <= 0 || r.h <= 0) return;
        std::vector<std::pair<float, Color>> st;
        if (dark) {
            st.push_back({0.f, Color::hex(0x2E2E2E, 0.40f)});
            st.push_back({1.f, Color::hex(0x1E1E1E, 0.28f)});
        } else {
            st.push_back({0.f, Color::hex(0xFFFFFF, 0.30f)});
            st.push_back({1.f, Color::hex(0xFAFAFA, 0.22f)});
        }
        fill_round_linear(r, 0.f, st);
    }

    // 圆角矩形 + 纵向线性渐变（磨砂面用，两段纯色叠会出硬边）
    void fill_round_linear(const Rect& r, float radius,
                           const std::vector<std::pair<float, Color>>& stops) {
        if (!rt_ || r.w <= 0 || r.h <= 0 || stops.size() < 2) return;
        std::vector<D2D1_GRADIENT_STOP> gs;
        gs.reserve(stops.size());
        for (const auto& s : stops) {
            Color c = s.second;
            if (premul_ && c.a < 0.999f) { c.r *= c.a; c.g *= c.a; c.b *= c.a; }
            D2D1_GRADIENT_STOP g{};
            g.position = s.first;
            g.color = d2d(c);
            gs.push_back(g);
        }
        Com<ID2D1GradientStopCollection> coll;
        if (FAILED(rt_->CreateGradientStopCollection(gs.data(), (UINT)gs.size(), D2D1_GAMMA_2_2,
                                                     D2D1_EXTEND_MODE_CLAMP, coll.put())))
            return;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
        props.startPoint = d2dp(Point{r.x, r.y});
        props.endPoint = d2dp(Point{r.x, r.y + r.h});
        Com<ID2D1LinearGradientBrush> gb;
        if (FAILED(rt_->CreateLinearGradientBrush(props, coll.get(), gb.put()))) return;
        float lim = (r.w < r.h ? r.w : r.h) * 0.5f;
        float rad = radius <= 0.5f ? 0.f : (radius > lim ? lim : radius);
        if (rad <= 0.f) rt_->FillRectangle(d2d(r), gb.get());
        else rt_->FillRoundedRectangle(D2D1_ROUNDED_RECT{d2d(r), rad, rad}, gb.get());
    }

    void fill_circle(Point c, float radius, Color col) {
        rt_->FillEllipse(D2D1_ELLIPSE{d2dp(c), radius, radius}, brush(col));
    }

    // 线性渐变填充：取色器的 HSV 方块和色相条要它。
    // stops 的 offset 是 0..1，沿 vertical ? 上→下 : 左→右 排。直角矩形（要圆角就自己裁）。
    void fill_linear(const Rect& r, const std::vector<std::pair<float, Color>>& stops,
                     bool vertical) {
        if (!rt_ || r.w <= 0 || r.h <= 0 || stops.size() < 2) return;
        std::vector<D2D1_GRADIENT_STOP> gs;
        gs.reserve(stops.size());
        for (const auto& s : stops) {
            Color c = s.second;
            if (premul_ && c.a < 0.999f) { c.r *= c.a; c.g *= c.a; c.b *= c.a; }
            D2D1_GRADIENT_STOP g{};
            g.position = s.first;
            g.color = d2d(c);
            gs.push_back(g);
        }
        Com<ID2D1GradientStopCollection> coll;
        if (FAILED(rt_->CreateGradientStopCollection(gs.data(), (UINT)gs.size(),
                                                     D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                                                     coll.put())))
            return;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
        props.startPoint = d2dp(Point{r.x, r.y});
        props.endPoint = vertical ? d2dp(Point{r.x, r.y + r.h}) : d2dp(Point{r.right(), r.y});
        Com<ID2D1LinearGradientBrush> gb;
        if (FAILED(rt_->CreateLinearGradientBrush(props, coll.get(), gb.put()))) return;
        rt_->FillRectangle(d2d(r), gb.get());
    }

    void line(Point a, Point b, Color c, float width = 1.f) {
        rt_->DrawLine(d2dp(a), d2dp(b), brush(c), width);
    }

    // 圆弧（进度环用）。角度制，0° 指向右侧，顺时针为正。
    void arc(Point c, float radius, float start_deg, float sweep_deg, float width, Color col) {
        if (radius <= 0.f || sweep_deg == 0.f) return;
        ID2D1Factory* fac = d2d_factory();
        if (!fac) return;
        Com<ID2D1PathGeometry> path;
        if (FAILED(fac->CreatePathGeometry(path.put()))) return;
        Com<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(sink.put()))) return;

        const float kDeg = 3.14159265358979323846f / 180.f;
        float a0 = start_deg * kDeg;
        float a1 = (start_deg + sweep_deg) * kDeg;
        D2D1_POINT_2F p0{d2d_Pointf(c, radius, a0)};
        D2D1_POINT_2F p1{d2d_Pointf(c, radius, a1)};
        sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
        D2D1_ARC_SEGMENT seg{};
        seg.point = p1;
        seg.size = D2D1_SIZE_F{radius, radius};
        seg.rotationAngle = 0.f;
        seg.sweepDirection = sweep_deg >= 0.f ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                             : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
        seg.arcSize = std::fabs(sweep_deg) >= 180.f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        sink->AddArc(seg);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        rt_->DrawGeometry(path.get(), brush(col), width);
    }

    // 廉价软阴影：多层同心的圆角矩形叠加模拟高斯模糊。
    // 光在左上角 → 只往右下投影：每层左上边都压在形状内侧（用 offset 退回来），
    // 右下边按 blur 逐层外推，所以左/上一点也不外溢，右下最多 offset+blur（默认 ≈7px）。
    // 透明度随层数单调衰减（越靠里越黑），外缘连续渐隐，不会出现硬边或一圈圈的色带。
    void shadow(const Rect& r, float radius, float blur, Color col, float offset = 2.5f,
                float offset_x = NAN) {
        if (col.a <= 0.001f || blur <= 0.f) return;
        if (std::isnan(offset_x)) offset_x = offset;
        const int layers = 16;
        for (int i = layers; i >= 1; --i) {
            float t = (float)i / (float)layers;          // 1 = 最外圈，越小越贴近形状
            Rect rr(r.x + offset_x, r.y + offset, r.w + blur * t, r.h + blur * t);
            Color c = col;
            float k = 1.f - t;
            c.a = col.a * k * k * (2.2f / (float)layers);  // 内圈浓、外圈淡，叠起来是一条平滑的衰减
            fill_round(rr, radius + blur * t * 0.55f, c);
        }
    }

    // ------------------------------------------------------------- 文字 --
    IDWriteTextFormat* format(float size, int weight, HAlign ha, VAlign va, bool wrap,
                              bool display, bool icons) {
        const std::wstring& fam = icons ? icon_family() : (display ? display_family() : body_family());
        char key[320];
        // 字体族和语言也进 key：运行时被 set_fonts / set_ui_locale 改过之后，旧格式不会继续被复用
        std::snprintf(key, sizeof(key), "%.2f|%d|%d|%d|%d|%d|%d|%ls|%ls", size, weight,
                      (int)ha, (int)va, wrap ? 1 : 0, display ? 1 : 0, icons ? 1 : 0,
                      fam.c_str(), ui_locale().c_str());
        auto it = formats_.find(key);
        if (it != formats_.end()) return it->second.get();

        Com<IDWriteTextFormat> fmt;
        IDWriteFactory* dw = dwrite_factory();
        if (!dw) return nullptr;
        dw->CreateTextFormat(fam.c_str(), nullptr, (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL,
                             DWRITE_FONT_STRETCH_NORMAL, size, ui_locale().c_str(), fmt.put());
        if (!fmt) return nullptr;

        fmt->SetTextAlignment(ha == HAlign::Start    ? DWRITE_TEXT_ALIGNMENT_LEADING
                              : ha == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                     : DWRITE_TEXT_ALIGNMENT_TRAILING);
        fmt->SetParagraphAlignment(va == VAlign::Top     ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                   : va == VAlign::Middle ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                          : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
        fmt->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteTextFormat* raw = fmt.get();
        formats_[key] = fmt;
        return raw;
    }

    // 文本布局缓存：CreateTextLayout 一帧要建几十个（菜单十几行 × 每行两个文本），
    // 不缓存的话每帧光建布局就要 10~20ms，动画就"卡卡的"。缓存键 = 文本 + 字号格式 + 矩形。
    IDWriteTextLayout* cached_layout(const std::wstring& s, IDWriteTextFormat* fmt, float w, float h,
                                     bool icons) {
        if (layouts_.size() > 800) layouts_.clear();   // 简单上限，别让长会话无限涨
        std::wstring key = s;
        key += L'\x1f';
        key += std::to_wstring((long long)(size_t)fmt);
        key += L'\x1f';
        key += std::to_wstring((long long)std::lround(w * 100.f));
        key += L'\x1f';
        key += std::to_wstring((long long)std::lround(h * 100.f));
        key += icons ? L'\x1f' : L'\x1e';
        auto it = layouts_.find(key);
        if (it != layouts_.end()) return it->second.get();
        IDWriteFactory* dw = dwrite_factory();
        if (!dw) return nullptr;
        Com<IDWriteTextLayout> lay;
        if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, w, h, lay.put())))
            return nullptr;
        IDWriteTextLayout* raw = lay.get();
        layouts_[key] = lay;
        return raw;
    }

    void text(const std::wstring& s, const Rect& r, TextScale sc, Color col,
              HAlign ha = HAlign::Start, VAlign va = VAlign::Middle, bool wrap = false,
              bool display = false) {
        if (s.empty() || r.w <= 0 || r.h <= 0) return;
        IDWriteTextFormat* fmt = format(text_scale_size(sc), text_scale_weight(sc), ha, va, wrap,
                                        display, false);
        if (!fmt) return;
        IDWriteTextLayout* lay = cached_layout(s, fmt, r.w, r.h, false);
        if (!lay) return;
        rt_->DrawTextLayout(D2D1_POINT_2F{r.x, r.y}, lay, brush(col),
                            D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    // 图标字形（Segoe Fluent Icons）。
    void glyph(const std::wstring& code, const Rect& r, float size, Color col,
               HAlign ha = HAlign::Center, VAlign va = VAlign::Middle) {
        if (code.empty() || r.w <= 0 || r.h <= 0) return;
        IDWriteTextFormat* fmt = format(size, 400, ha, va, false, false, true);
        if (!fmt) return;
        IDWriteTextLayout* lay = cached_layout(code, fmt, r.w, r.h, true);
        if (!lay) return;
        rt_->DrawTextLayout(D2D1_POINT_2F{r.x, r.y}, lay, brush(col),
                            D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    // 文本测量，用于布局；wrap=false 时返回单行宽度。
    Size measure_text(const std::wstring& s, TextScale sc, bool wrap = false, float max_w = 1e6f,
                      bool display = false) {
        if (s.empty()) return Size(0.f, text_scale_size(sc) * 1.35f);
        IDWriteTextFormat* fmt =
            format(text_scale_size(sc), text_scale_weight(sc), HAlign::Start, VAlign::Top, wrap,
                   display, false);
        if (!fmt) return Size(0.f, text_scale_size(sc) * 1.35f);
        IDWriteFactory* dw = dwrite_factory();
        Com<IDWriteTextLayout> lay;
        if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt,
                                        wrap ? max_w : 1e6f, 1e6f, lay.put())))
            return Size(0.f, text_scale_size(sc) * 1.35f);
        DWRITE_TEXT_METRICS m{};
        lay->GetMetrics(&m);
        return Size(m.widthIncludingTrailingWhitespace, m.height);
    }

    float line_height(TextScale sc) { return text_scale_size(sc) * 1.32f; }

    // ------------------------------------------------------------- 位图 --
    // 解码一次就按路径缓存，之后每帧只是 DrawBitmap。支持 png / jpg / bmp / gif 首帧
    // （WIC 解出来的都行）。路径给的是绝对路径或相对当前目录。
    ID2D1Bitmap* load_bitmap(const std::wstring& path) {
        if (path.empty() || !rt_) return nullptr;
        auto it = bitmaps_.find(path);
        if (it != bitmaps_.end()) return it->second.get();
        IWICImagingFactory* fac = wic_factory();
        if (!fac) return nullptr;
        Com<IWICBitmapDecoder> dec;
        if (FAILED(fac->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, dec.put())))
            return nullptr;
        Com<IWICBitmapFrameDecode> frame;
        if (FAILED(dec->GetFrame(0, frame.put()))) return nullptr;
        Com<IWICFormatConverter> conv;
        if (FAILED(fac->CreateFormatConverter(conv.put()))) return nullptr;
        if (FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0.f,
                                    WICBitmapPaletteTypeCustom)))
            return nullptr;
        Com<ID2D1Bitmap> bmp;
        D2D1_BITMAP_PROPERTIES props{};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                              D2D1_ALPHA_MODE_PREMULTIPLIED);
        props.dpiX = props.dpiY = 96.f;
        if (FAILED(rt_->CreateBitmapFromWicBitmap(conv.get(), props, bmp.put()))) return nullptr;
        ID2D1Bitmap* raw = bmp.get();
        bitmaps_[path] = bmp;
        return raw;
    }

    // 位图原始尺寸（像素）
    Size bitmap_size(ID2D1Bitmap* bmp) {
        if (!bmp) return Size(0.f, 0.f);
        D2D1_SIZE_F s = bmp->GetSize();
        return Size(s.width, s.height);
    }

    // 把整张位图缩放到 dst（会被拉扁/拉长，等比由调用方先算好）
    void draw_bitmap(ID2D1Bitmap* bmp, const Rect& dst, float opacity = 1.f) {
        if (!bmp || !rt_ || dst.w <= 0.f || dst.h <= 0.f) return;
        D2D1_RECT_F d = d2d(dst);
        if (opacity >= 0.999f) {
            rt_->DrawBitmap(bmp, &d, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            push_opacity(opacity, dst);
            rt_->DrawBitmap(bmp, &d, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            pop_opacity();
        }
    }

    // 只取源区域 src_px（位图像素）贴到 dst —— cover 裁剪用这个
    void draw_bitmap_src(ID2D1Bitmap* bmp, const Rect& dst, const Rect& src_px,
                         float opacity = 1.f) {
        if (!bmp || !rt_ || dst.w <= 0.f || dst.h <= 0.f) return;
        D2D1_RECT_F d = d2d(dst);
        D2D1_RECT_F s = d2d(src_px);
        if (opacity >= 0.999f) {
            rt_->DrawBitmap(bmp, &d, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &s);
        } else {
            push_opacity(opacity, dst);
            rt_->DrawBitmap(bmp, &d, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &s);
            pop_opacity();
        }
    }

    void drop_bitmap_cache() { bitmaps_.clear(); }

private:
    static D2D1_POINT_2F d2d_Pointf(Point c, float rad, float angle) {
        return D2D1_POINT_2F{c.x + rad * std::cos(angle), c.y + rad * std::sin(angle)};
    }

    ID2D1RenderTarget* rt_ = nullptr;
    Palette pal_{};
    float scale_ = 1.f;
    bool premul_ = false;   // 目标是不是 PREMULTIPLIED（分层窗口用）
    Com<ID2D1Layer> layer_;  // push_opacity 用的层
    bool layer_open_ = false;
    std::map<uint32_t, Com<ID2D1SolidColorBrush>> brushes_;
    std::map<std::string, Com<IDWriteTextFormat>> formats_;
    std::map<std::wstring, Com<IDWriteTextLayout>> layouts_;   // 每帧复用，避免重复建布局
    std::map<std::wstring, Com<ID2D1Bitmap>> bitmaps_;         // 位图按路径缓存（解码一次）
};

}  // namespace w11ui
