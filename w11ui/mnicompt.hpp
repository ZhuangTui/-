// w11ui / mnicompt.hpp ---------------------------------------------------------
// 小组件（mini components）：取色器、日期选择器、月历视图、数字小键盘。
// 它们比基础组件"大一号"，但规矩一样：程序唤起、程序给格式、程序拿结果，
// 库里没有任何固定文案——键位 / 星期名 / 标题 / 按钮字全部从参数来。
//
// 依赖 widgets.hpp（Widget / PopupPanel / TextBox / Opts / kw 都在那边）。
#pragma once

#include "widgets.hpp"

namespace w11ui {

// ------------------------------------------------------------ DatePicker ----
// 日历面板。**唤起是程序的事**：程序在自己的按钮/输入框动作里调 Window::pick_date(box, ...)，
// 库不会自己弹。面板本身是 PopupPanel 的派生类，于是它用的是同一套东西：
// 同一套分层弹层窗口、同一套向下/向上展开动画、同一套阴影圆角描边，
// 观感和下拉框、菜单是一模一样的，不和主窗口共用一个窗口。
//
// 所有可见文字都从外面来（星期名、"清除"/"今天"、标题格式、回填格式）；
// 库只给一套英文缩写当默认，随时可以整体换掉。
class DatePicker : public PopupPanel {
public:
    // ---- 程序可改的外观/文案 ----
    std::wstring title_pattern = L"yyyy-MM";   // 标题格式（M / MM 数字月，MMM 要用 month_names）
    std::vector<std::wstring> week_names;      // 7 个，从周日开始排
    std::vector<std::wstring> month_names;     // 12 个；空 = 标题用数字月
    std::wstring clear_text;                   // 左下角（空 = 不画这个按钮）
    std::wstring today_text;                   // 右下角（空 = 不画这个按钮）
    int  week_start = 1;                       // 表头从周几起：0 = 周日，1 = 周一
    float days_width = 340.f;                  // 面板宽度
    std::wstring date_format = L"yyyy-MM-dd";  // 选完写回输入框用的格式（程序指定，默认 4y-2m-2d）

    // ---- 状态 ----
    int year = 2026, month = 1;                // 现在翻到哪个月
    int sel_y = 0, sel_m = 0, sel_d = 0;       // 选中的日期（0 = 没选）
    int today_y = 0, today_m = 0, today_d = 0; // 系统今天（只用来做"今天"按钮和圈）

    // 选完 / 清除时回调：清除给 (0,0,0)
    std::function<void(int, int, int)> on_pick_date;

    // ---- 布局常数（要改尺寸就改这几个）----
    float pad    = 12.f;
    float head_h = 42.f;
    float week_h = 26.f;
    float cell_h = 36.f;
    int   rows   = 6;
    float foot_h = 40.f;

    // ---- 翻月的滚动过渡（上下箭头点了以后日期区上下滑，不是瞬间换一屏）----
    mutable int    slide_dir_ = 0;   // +1 下一月、-1 上一月、0 静止（const 的 animating() 里要收尾）
    mutable double slide_t0_ = -1.0;
    double slide_ms      = 220.0;
    int    slide_from_y_ = 0, slide_from_m_ = 0;
    bool   slide_enabled = true;
    float  pin_slide_e   = -1.f;     // 出图用：>0 时把滚动钉在这个进度

    // 面板内部还在动 → PopupWin 继续出帧（不然弹层展开完就停表，滚动看不见）
    bool animating() const override {
        if (pin_slide_e >= 0.f) return false;
        if (slide_t0_ < 0.0) return false;
        // 多留 20ms：保证"进度到 100%"的那一帧也被画出来，停在中途就尴尬了
        if (now_ms() - slide_t0_ < slide_ms + 20.0) return true;
        slide_t0_ = -1.0;      // 收尾（mutable：这层是 const 的，但该清就得清）
        slide_dir_ = 0;
        return false;
    }

    // 弹层每帧在 CPU 里拼像素时要的进度 / 方向
    float motion_e() const override {
        if (pin_slide_e >= 0.f) return pin_slide_e;
        if (slide_t0_ < 0.0) return 1.f;
        float k = (float)((now_ms() - slide_t0_) / (slide_ms > 0.0 ? slide_ms : 1.0));
        if (k >= 1.f) return 1.f;
        return ease_out_cubic(k);
    }
    int motion_dir() const override { return slide_dir_ >= 0 ? 1 : -1; }

    void pin_slide(float e, int from_y, int from_m) {
        slide_from_y_ = from_y; slide_from_m_ = from_m;
        slide_dir_ = 1;
        slide_t0_ = -1.0;
        pin_slide_e = std::clamp(e, 0.f, 1.f);
        mark_dirty();
    }

    float preferred_width() const { return days_width; }
    float preferred_height() const {
        float h = pad * 2.f + head_h + week_h + cell_h * (float)rows;
        if (!clear_text.empty() || !today_text.empty()) h += 8.f + 1.f + foot_h;
        return h;
    }

    DatePicker() {
        cast_shadow = true;
        radius = Radius::flyout;
        week_names = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
        read_today(today_y, today_m, today_d);
        year = today_y;
        month = today_m;
    }

    template <class... Args>
    explicit DatePicker(Args&&... args) : DatePicker() {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        if (!o.title_pattern.empty()) title_pattern = o.title_pattern;
        if (o.week_names.size() == 7) week_names = o.week_names;
        if (o.month_names.size() == 12) month_names = o.month_names;
        clear_text = o.clear_text;
        today_text = o.today_text;
        week_start = o.week_start ? 1 : 0;
        if (!std::isnan(o.width) && o.width > 100.f) days_width = o.width;
        date_format = o.date_format.empty() ? std::wstring(L"yyyy-MM-dd") : o.date_format;
        on_pick_date = o.on_pick_date;
        // 程序给了初始日期就用它，否则停在"今天"所在的那个月
        if (o.year > 0 && o.month >= 1 && o.month <= 12) {
            year = o.year;
            month = o.month;
            if (o.day > 0) { sel_y = o.year; sel_m = o.month; sel_d = o.day; }
        }
    }

    // ---- 程序用的入口（比 set 成员更省事，也只改状态不动画面）----
    void set_date(int y, int m, int d) {
        if (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= days_in_month(y, m)) {
            sel_y = y; sel_m = m; sel_d = d;
            year = y; month = m;
        } else {
            sel_y = sel_m = sel_d = 0;
        }
        mark_dirty();
    }
    void goto_month(int y, int m) {
        if (m < 1 || m > 12) return;
        year = y; month = m;
        slide_t0_ = -1.0; slide_dir_ = 0;
        mark_dirty();
    }

    // ---- 布局：所有命中矩形都在这里算，paint 和 click 用同一套 ----
    void arrange(Painter& p, const Rect& r) override {
        (void)p;
        bounds = r;
        float inner_w = r.w - pad * 2.f;
        cell_w  = inner_w / 7.f;
        grid_x  = r.x + pad;
        head_y  = r.y + pad;
        week_y  = head_y + head_h;
        grid_y  = week_y + week_h;
        foot_y  = r.y + r.h - pad - (clear_text.empty() && today_text.empty() ? 0.f : foot_h);
        float nav = 28.f;
        prev_r = Rect(r.right() - pad - nav * 2.f - 4.f, head_y + (head_h - nav) * 0.5f, nav, nav);
        next_r = Rect(r.right() - pad - nav,              head_y + (head_h - nav) * 0.5f, nav, nav);
        float btn_w = 66.f;
        clear_r = Rect(r.x + pad, foot_y, btn_w, foot_h);
        today_r = Rect(r.right() - pad - btn_w, foot_y, btn_w, foot_h);
    }

    struct Ymd { int y, m, d; };

    // 这几块命中矩形（arrange 算好的），程序想自己贴按钮/自测都能读
    Rect nav_prev_rect()  const { return prev_r; }
    Rect nav_next_rect()  const { return next_r; }
    Rect clear_btn_rect() const { return clear_r; }
    Rect today_btn_rect() const { return today_r; }

    // 第 i 格（0..41，从左到右、从上到下）对应哪一天（指定年月版，滚动时要画两套）
    Ymd date_of_m(int yy, int mm, int i) const {
        int wd = weekday_of(yy, mm, 1);                         // 0 = 周日
        int lead = (week_start == 0) ? wd : ((wd + 6) % 7);      // 1 号前面空几格
        int day = 1 + (i - lead);
        int y = yy, m = mm;
        while (day < 1) { --m; if (m < 1) { m = 12; --y; } day += days_in_month(y, m); }
        while (day > days_in_month(y, m)) { day -= days_in_month(y, m); ++m; if (m > 12) { m = 1; ++y; } }
        return {y, m, day};
    }
    Ymd date_of(int i) const { return date_of_m(year, month, i); }

    Rect cell_rect(int i) const {
        int row = i / 7, col = i % 7;
        return Rect(grid_x + (float)col * cell_w, grid_y + (float)row * cell_h, cell_w, cell_h);
    }

    // 命中：1 = 清除，2 = 今天，3 = 上一月，4 = 下一月，0 = 都不是
    int hit_button(Point pt) const {
        if (prev_r.contains(pt)) return 3;
        if (next_r.contains(pt)) return 4;
        if (!clear_text.empty() && clear_r.contains(pt)) return 1;
        if (!today_text.empty() && today_r.contains(pt)) return 2;
        return 0;
    }
    int cell_at(Point pt) const {
        if (pt.x < grid_x || pt.x > grid_x + cell_w * 7.f) return -1;
        if (pt.y < grid_y || pt.y > grid_y + cell_h * (float)rows) return -1;
        int col = (int)((pt.x - grid_x) / cell_w);
        int row = (int)((pt.y - grid_y) / cell_h);
        if (col < 0 || col > 6 || row < 0 || row >= rows) return -1;
        return row * 7 + col;
    }

    // 画一整套 6×7 日期（指定年月），dy 是整体竖直偏移 —— 翻月滚动时同一套画两遍
    void paint_grid(Painter& p, int yy, int mm, float dy) {        const Palette& pal = p.palette();
        for (int i = 0; i < rows * 7; ++i) {
            Ymd d = date_of_m(yy, mm, i);
            Rect cell = cell_rect(i);
            cell.y += dy;
            bool in_month = (d.m == mm && d.y == yy);
            bool is_sel   = (sel_y != 0 && d.y == sel_y && d.m == sel_m && d.d == sel_d);
            bool is_today = (today_y != 0 && d.y == today_y && d.m == today_m && d.d == today_d);

            // 选中块是正方形（参考图里 11 号那块不是扁的），悬停用同一块形状
            float side = std::min(cell_w, cell_h) - 8.f;
            Rect sq(cell.x + (cell.w - side) * 0.5f, cell.y + (cell.h - side) * 0.5f, side, side);
            if (i == hover_cell_ && yy == year && mm == month && !is_sel)
                p.fill_round(sq, Radius::control, pal.flyout_hover);
            if (is_sel) {
                p.fill_round(sq, Radius::control, pal.accent);
            } else if (is_today) {
                // 今天没被选中时只描一圈，不抢选中块的视觉
                p.stroke_round(sq, Radius::control, pal.accent.alpha(0.55f), 1.f);
            }
            Color tc = is_sel ? pal.on_accent
                      : (in_month ? (is_today ? pal.accent : pal.text) : pal.text_tertiary);
            p.text(std::to_wstring(d.d), cell, TextScale::Body, tc,
                   HAlign::Center, VAlign::Middle, false);
        }
    }

    // 标题 + 两个翻月箭头 + 星期表头（翻月时全都会变，所以和日期区一起算"动区"）
    void draw_head(Painter& p) {
        const Palette& pal = p.palette();
        float title_w = prev_r.x - (bounds.x + pad) - 8.f;
        p.text(title_text(), Rect(bounds.x + pad, head_y, title_w, head_h),
               TextScale::Subtitle, pal.text, HAlign::Start, VAlign::Middle, false);

        // 翻月：上箭头 = 上一月，下箭头 = 下一月（Win11 就是这两个字形）
        draw_nav(p, prev_r, glyphs().chevron_open, hover_btn_ == 3, pal);
        draw_nav(p, next_r, glyphs().chevron,      hover_btn_ == 4, pal);

        for (int c = 0; c < 7; ++c) {
            p.text(week_label(c), Rect(grid_x + (float)c * cell_w, week_y, cell_w, week_h),
                   TextScale::Caption, pal.text_tertiary, HAlign::Center, VAlign::Middle, false);
        }
    }

    // 日期区（含翻月滚动）就画这一处：整块 paint 和"只重画这一小块"共用它，
    // 两边的像素才会完全一致。
    void draw_dates(Painter& p) {
        float vh = cell_h * (float)rows;
        Rect view(grid_x, grid_y, cell_w * 7.f, vh);

        bool sliding = false;
        float e = 1.f;
        if (pin_slide_e >= 0.f) {
            sliding = true;
            e = pin_slide_e;
        } else if (slide_dir_ != 0 && slide_t0_ >= 0.0) {
            float k = (float)((now_ms() - slide_t0_) / (slide_ms > 0.0 ? slide_ms : 1.0));
            if (k < 1.f) {
                sliding = true;
                e = ease_out_cubic(k);
            }
            // k >= 1：按静态画。状态由 animating() 收，别在这里清 ——
            // 这里清了 motion_area 的"在动"判断会立刻转假，最后一帧（终态）就没人画了。
        }

        if (sliding) {
            p.push_clip(view);
            if (slide_dir_ >= 0) {
                paint_grid(p, slide_from_y_, slide_from_m_, -e * vh);
                paint_grid(p, year, month, (1.f - e) * vh);
            } else {
                paint_grid(p, slide_from_y_, slide_from_m_, e * vh);
                paint_grid(p, year, month, -(1.f - e) * vh);
            }
            p.pop_clip();
        } else {
            paint_grid(p, year, month, 0.f);
        }
    }

    Rect dates_rect() const { return Rect(grid_x, grid_y, cell_w * 7.f, cell_h * (float)rows); }

    // 日期区是"会自己变的那一块"：弹层每次整块重建后都给它留一张像素，
    // 翻月滚动时就在 CPU 里把切之前 / 切之后两张按进度拼起来 —— 不进 D2D，一帧 0.1 ms。
    bool motion_area(Rect& r) const override {
        r = dates_rect();
        return true;
    }

    std::wstring title_text() const {
        return format_date(year, month, 1, title_pattern,
                           month_names.size() >= 12 ? &month_names : nullptr);
    }
    std::wstring week_label(int c) const {
        int idx = (week_start + c) % 7;
        if (idx < 0) idx += 7;
        return (idx < (int)week_names.size()) ? week_names[(std::size_t)idx] : std::wstring();
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        draw_shadow(p);
        p.fill_round(bounds, radius, pal.flyout);
        p.stroke_round(bounds.inset(0.5f), radius, pal.flyout_border, 1.f);

        // 标题（左边粗体）+ 翻月箭头 + 星期表头（翻月时整块内容都会变，所以一起抽出去）
        draw_head(p);

        // 6×7 日期格：翻月时这一块上下滚动（旧月往上走、新月从下面进来）
        draw_dates(p);

        // 底栏：一条细线 + 左右两个文字按钮（文字为空就不画）
        if (!clear_text.empty() || !today_text.empty()) {
            p.fill_rect(Rect(bounds.x + pad, foot_y - 8.f, bounds.w - pad * 2.f, 1.f), pal.flyout_sep);
            if (!clear_text.empty())
                p.text(clear_text, clear_r, TextScale::Body,
                       hover_btn_ == 1 ? pal.accent : pal.text_secondary,
                       HAlign::Start, VAlign::Middle, false);
            if (!today_text.empty())
                p.text(today_text, today_r, TextScale::Body,
                       hover_btn_ == 2 ? pal.accent_hover : pal.accent,
                       HAlign::End, VAlign::Middle, false);
        }
    }

    virtual void mouse_move(Point pt) override {
        int b = hit_button(pt);
        int c = cell_at(pt);
        if (b != hover_btn_ || c != hover_cell_) {
            hover_btn_ = b;
            hover_cell_ = c;
            mark_dirty();
        }
    }
    virtual void mouse_leave() override {
        if (hover_btn_ != 0 || hover_cell_ != -1) {
            hover_btn_ = 0;
            hover_cell_ = -1;
            mark_dirty();
        }
    }

    virtual void click(Point pt) override {
        int b = hit_button(pt);
        if (b == 1) { pick(0, 0, 0); return; }                    // 清除
        if (b == 2) { pick(today_y, today_m, today_d); return; }  // 今天
        if (b == 3) { shift_month(-1); return; }
        if (b == 4) { shift_month(1); return; }
        int c = cell_at(pt);
        if (c < 0) return;
        Ymd d = date_of(c);
        pick(d.y, d.m, d.d);
    }

    void pick(int y, int m, int d) {
        if (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= days_in_month(y, m)) {
            sel_y = y; sel_m = m; sel_d = d;
            year = y; month = m;
        } else {
            sel_y = sel_m = sel_d = 0;
        }
        slide_t0_ = -1.0; slide_dir_ = 0;      // 选了日期就停住滚动，别让残影盖着
        mark_dirty();
        if (on_pick_date) on_pick_date(sel_y, sel_m, sel_d);
    }

    void shift_month(int k) {
        int from_y = year, from_m = month;
        month += k;
        while (month < 1) { month += 12; --year; }
        while (month > 12) { month -= 12; ++year; }
        if (slide_enabled) {
            slide_from_y_ = from_y; slide_from_m_ = from_m;
            slide_dir_ = k >= 0 ? 1 : -1;
            slide_t0_ = now_ms();
            // 这一次 mark_dirty 是"整块重建一次"：标题、表头、新月日期一次到位，
            // 顺带让弹层把切月前/后的日期区像素各截一张（后面 200ms 全靠这两张拼）。
        }
        mark_dirty();   // 关掉滚动时它就是"瞬间换一屏"，本来也要重画
    }

private:
    void draw_nav(Painter& p, const Rect& r, const std::wstring& g, bool hov, const Palette& pal) {
        if (hov) p.fill_round(r, Radius::control, pal.flyout_hover);
        p.glyph(g, r, 11.f, hov ? pal.text : pal.text_secondary);
    }
    static void read_today(int& y, int& m, int& d) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        y = (int)st.wYear; m = (int)st.wMonth; d = (int)st.wDay;
    }

    float cell_w = 45.f;
    float grid_x = 0.f, head_y = 0.f, week_y = 0.f, grid_y = 0.f, foot_y = 0.f;
    Rect  prev_r, next_r, clear_r, today_r;
    int   hover_btn_ = 0;
    int   hover_cell_ = -1;
};

// ------------------------------------------------------------ 颜色小工具 ----
// 这几个纯函数既给取色器用，程序也能直接用（自己格式化、自己反解）。
inline void hsv_to_rgb(float h, float s, float v, int& r, int& g, int& b) {
    h = std::fmod(h, 360.f); if (h < 0.f) h += 360.f;
    s = s < 0.f ? 0.f : (s > 1.f ? 1.f : s);
    v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    float c = v * s;
    float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    float m = v - c, rr = 0.f, gg = 0.f, bb = 0.f;
    if      (h <  60.f) { rr = c; gg = x; }
    else if (h < 120.f) { rr = x; gg = c; }
    else if (h < 180.f) { gg = c; bb = x; }
    else if (h < 240.f) { gg = x; bb = c; }
    else if (h < 300.f) { rr = x; bb = c; }
    else                { rr = c; bb = x; }
    r = (int)std::lround((rr + m) * 255.f);
    g = (int)std::lround((gg + m) * 255.f);
    b = (int)std::lround((bb + m) * 255.f);
}

inline void rgb_to_hsv(int r, int g, int b, float& h, float& s, float& v) {
    float rr = r / 255.f, gg = g / 255.f, bb = b / 255.f;
    float mx = std::max(rr, std::max(gg, bb)), mn = std::min(rr, std::min(gg, bb));
    float d = mx - mn;
    v = mx;
    s = (mx <= 0.0001f) ? 0.f : d / mx;
    if (d <= 0.0001f) { h = 0.f; return; }
    if      (mx == rr) h = 60.f * std::fmod((gg - bb) / d, 6.f);
    else if (mx == gg) h = 60.f * ((bb - rr) / d + 2.f);
    else               h = 60.f * ((rr - gg) / d + 4.f);
    if (h < 0.f) h += 360.f;
}

inline int hex2(int v) { return (v < 0 ? 0 : (v > 255 ? 255 : v)); }

inline std::wstring hex2w(int v, bool upper = true) {
    static const wchar_t* dL = L"0123456789abcdef";
    static const wchar_t* dU = L"0123456789ABCDEF";
    const wchar_t* d = upper ? dU : dL;
    v = hex2(v);
    std::wstring s;
    s += d[(v >> 4) & 0xF];
    s += d[v & 0xF];
    return s;
}

// 按 pattern 把颜色写成一串。令牌：
//   {r} {g} {b}      十进制 0..255        {R} {G} {B}     两位大写十六进制
//   {hex}            RRGGBB（大写）        {hexl}           小写
//   {h} {s} {v}      色相 0..360 / 饱和 0..100 / 明度 0..100
//   其它字符原样输出。默认 "#{hex}"。
inline std::wstring format_color(int r, int g, int b, const std::wstring& pattern) {
    r = hex2(r); g = hex2(g); b = hex2(b);
    std::wstring out;
    for (std::size_t i = 0; i < pattern.size();) {
        if (pattern[i] != L'{') { out += pattern[i++]; continue; }
        std::size_t j = pattern.find(L'}', i);
        if (j == std::wstring::npos) { out += pattern.substr(i); break; }
        std::wstring t = pattern.substr(i + 1, j - i - 1);
        if      (t == L"r") out += std::to_wstring(r);
        else if (t == L"g") out += std::to_wstring(g);
        else if (t == L"b") out += std::to_wstring(b);
        else if (t == L"R") out += hex2w(r);
        else if (t == L"G") out += hex2w(g);
        else if (t == L"B") out += hex2w(b);
        else if (t == L"hex")  out += hex2w(r) + hex2w(g) + hex2w(b);
        else if (t == L"hexl") out += hex2w(r, false) + hex2w(g, false) + hex2w(b, false);
        else if (t == L"h" || t == L"s" || t == L"v") {
            float hh = 0.f, ss = 0.f, vv = 0.f;
            rgb_to_hsv(r, g, b, hh, ss, vv);
            float n = (t == L"h") ? hh : (t == L"s" ? ss * 100.f : vv * 100.f);
            out += std::to_wstring((int)std::lround(n));
        } else {
            out += pattern.substr(i, j - i + 1);   // 不认识的令牌原样留着
        }
        i = j + 1;
    }
    return out;
}

// 宽松反解：程序给的串可能来自任何地方，所以不按 pattern 卡死。
//   "#0F6CBD" / "0f6cbd" / "rgb(15,108,189)" / "15 108 189" / "15,108,189" 都认。
inline bool parse_color(const std::wstring& text, int& r, int& g, int& b) {
    // 先去掉 0x / 0X 前缀（程序常这么写）
    std::wstring src = text;
    for (std::size_t p = src.find(L"0x"); p != std::wstring::npos; p = src.find(L"0x")) src.erase(p, 2);
    for (std::size_t p = src.find(L"0X"); p != std::wstring::npos; p = src.find(L"0X")) src.erase(p, 2);

    std::wstring hx;
    std::vector<int> nums;
    int cur = -1;
    for (wchar_t ch : src) {
        bool isHex = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F');
        if (ch >= L'0' && ch <= L'9') {
            if (cur < 0) cur = 0;
            cur = cur * 10 + (ch - L'0');
            if (cur > 255) cur = 255;          // 溢出的十进制数字不当作颜色分量
        } else if (isHex) {
            hx += (wchar_t)towlower(ch);
            if (cur >= 0) { nums.push_back(cur); cur = -1; }
        } else {
            if (cur >= 0) { nums.push_back(cur); cur = -1; }
        }
    }
    if (cur >= 0) nums.push_back(cur);

    // 先按十进制三分量（0..255）
    if (nums.size() >= 3 && nums[0] <= 255 && nums[1] <= 255 && nums[2] <= 255) {
        r = nums[0]; g = nums[1]; b = nums[2];
        return true;
    }
    // 否则当十六进制整串
    std::wstring all;
    for (wchar_t ch : src)
        if ((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F'))
            all += (wchar_t)towlower(ch);
    if (all.size() == 6 || all.size() == 8) {
        std::size_t off = all.size() - 6;           // 8 位当 AARRGGBB，丢掉 alpha
        auto part = [&](std::size_t i) {
            wchar_t a = all[off + i], c = all[off + i + 1];
            auto v = [](wchar_t x) { return (x >= L'0' && x <= L'9') ? (x - L'0') : (x - L'a' + 10); };
            return (int)(v(a) * 16 + v(c));
        };
        r = part(0); g = part(2); b = part(4);
        return true;
    }
    return false;
}

// ----------------------------------------------------------- ColorPicker ----
// 取色器面板。**唤起是程序的事**：程序在按钮/色块/输入框的动作里调
// Window::pick_color(...)，把「回填到哪」（输入框，或只收回调自己写变量）和
// 「回填成什么格式」一起传进去。面板复用下拉框那一套弹层窗口与展开动画。
//
// 可见文字同样全部外部注入：标题、确定/取消、格式串都从参数来；默认不画按钮文字，
// 所以不传就没有"确定/取消"字样。
class ColorPicker : public PopupPanel {
public:
    // ---- 程序可改 ----
    std::wstring title;                        // 面板标题（空 = 不画标题、不占高）
    std::wstring color_format = L"#{hex}";     // 回填格式（默认 #RRGGBB）
    std::wstring ok_text, cancel_text;         // 空 = 不画那个按钮
    std::vector<unsigned> swatches;            // 预设色板 0xRRGGBB（空 = 不画这一行）
    float panel_w = 320.f;
    std::function<void(int, int, int)> on_pick_color;   // 确定/点色板时给 (r,g,b)
    std::function<void()> on_close_request;             // 取消 / 关面板（由 Window 接上）

    // 吸管：标题行右侧那个按钮。按住它拖到屏幕任意位置，松手就取那一颗像素。
    bool  allow_dropper = true;      // false = 不画吸管按钮
    bool  dropping_ = false;         // 正在按住吸管取色
    bool  drop_moved_ = false;       // 拖过才算取色；只点一下不算
    Point drop_from_;
    Rect  pick_r;

    // ---- 状态（HSV 是主状态，RGB 由它算出来）----
    float hue = 205.f, sat = 0.92f, val = 0.74f;
    int   r = 15, g = 108, b = 91;
    bool  color_given = false;        // 程序有没有显式给过初始色（没给才好从输入框反解）

    // ---- 布局常数 ----
    float pad     = 14.f;
    float head_h  = 30.f;
    float sv_h    = 168.f;
    float hue_w   = 16.f;
    float gap     = 10.f;
    float value_h = 30.f;
    float sw_h    = 26.f;
    float foot_h  = 38.f;

    float preferred_width() const { return panel_w; }
    float preferred_height() const {
        float h = pad * 2.f + 8.f + sv_h + 8.f + value_h;
        if (!title.empty()) h += head_h;
        if (!swatches.empty()) h += sw_h + gap;
        if (!ok_text.empty() || !cancel_text.empty()) h += 10.f + 1.f + foot_h;
        return h;
    }

    ColorPicker() { cast_shadow = true; radius = Radius::flyout; sync_rgb(); }

    template <class... Args>
    explicit ColorPicker(Args&&... args) : ColorPicker() {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        title = o.title;
        ok_text = o.ok_text;
        cancel_text = o.cancel_text;
        if (!o.color_format.empty()) color_format = o.color_format;
        swatches = o.swatches;
        if (!std::isnan(o.width) && o.width > 120.f) panel_w = o.width;
        on_pick_color = o.on_pick_color;
        if (o.color >= 0) {
            set_rgb((o.color >> 16) & 0xFF, (o.color >> 8) & 0xFF, o.color & 0xFF);
            color_given = true;
        }
    }

    // ---- 程序用的入口 ----
    void set_rgb(int rr, int gg, int bb) {
        r = hex2(rr); g = hex2(gg); b = hex2(bb);
        rgb_to_hsv(r, g, b, hue, sat, val);
        mark_dirty();
    }
    void set_hsv(float h, float s, float v) {
        hue = h; sat = s; val = v;
        sync_rgb();
        mark_dirty();
    }
    int color_rgb() const { return (r << 16) | (g << 8) | b; }

    // ---- 布局 ----
    void arrange(Painter& p, const Rect& r) override {
        (void)p;
        bounds = r;
        float y = r.y + pad;
        if (!title.empty()) {
            title_r = Rect(r.x + pad, y, r.w - pad * 2.f - (allow_dropper ? 28.f : 0.f), head_h);
            pick_r  = allow_dropper ? Rect(r.right() - pad - 24.f, y + (head_h - 24.f) * 0.5f, 24.f, 24.f)
                                    : Rect();
            y += head_h;
        } else {
            title_r = Rect();
            pick_r  = Rect();
        }
        y += 8.f;
        float sv_w = r.w - pad * 2.f - hue_w - gap;
        sv_r  = Rect(r.x + pad, y, sv_w, sv_h);
        hue_r = Rect(r.x + pad + sv_w + gap, y, hue_w, sv_h);
        y += sv_h + 8.f;
        value_r = Rect(r.x + pad, y, r.w - pad * 2.f, value_h);
        preview_r = Rect(r.x + pad, y + (value_h - 22.f) * 0.5f, 22.f, 22.f);
        y += value_h;
        if (!swatches.empty()) {
            y += gap;
            float n = (float)swatches.size();
            float cw = (r.w - pad * 2.f) / n;
            sw_r = Rect(r.x + pad, y + 3.f, r.w - pad * 2.f, sw_h - 6.f);
            sw_cw = cw;
        }
        if (!ok_text.empty() || !cancel_text.empty())
            foot_y = r.y + r.h - pad - foot_h;
        else
            foot_y = r.y + r.h;
        float bw = 84.f;
        cancel_r = Rect(r.right() - pad - bw * 2.f - 10.f, foot_y, bw, foot_h);
        ok_r     = Rect(r.right() - pad - bw,               foot_y, bw, foot_h);
    }

    // 命中：0 无，1 SV 方块，2 色相条，3 确定，4 取消，5+ 第 n 个色板，100 吸管
    int hit(Point pt) const {
        if (allow_dropper && pick_r.w > 0.f && pick_r.contains(pt)) return 100;
        if (sv_r.contains(pt)) return 1;
        if (hue_r.contains(pt)) return 2;
        if (!ok_text.empty() && ok_r.contains(pt)) return 3;
        if (!cancel_text.empty() && cancel_r.contains(pt)) return 4;
        if (!swatches.empty() && sw_r.contains(pt)) {
            int i = (int)((pt.x - sw_r.x) / std::max(1.f, sw_cw));
            if (i >= 0 && i < (int)swatches.size()) return 5 + i;
        }
        return 0;
    }

    // 把点挪到取色点（点哪儿、拖到哪儿都走这里）
    void drag_to(Point pt) {
        if (sv_r.contains(pt) || drag_zone_ == 1) {
            float sx = std::min(1.f, std::max(0.f, (pt.x - sv_r.x) / std::max(1.f, sv_r.w)));
            float vy = std::min(1.f, std::max(0.f, 1.f - (pt.y - sv_r.y) / std::max(1.f, sv_r.h)));
            sat = sx; val = vy;
            sync_rgb();
            mark_dirty();
        } else if (hue_r.contains(pt) || drag_zone_ == 2) {
            float hx = std::min(1.f, std::max(0.f, (pt.y - hue_r.y) / std::max(1.f, hue_r.h)));
            hue = hx * 360.f;
            sync_rgb();
            mark_dirty();
        }
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        draw_shadow(p);
        p.fill_round(bounds, radius, pal.flyout);
        p.stroke_round(bounds.inset(0.5f), radius, pal.flyout_border, 1.f);

        if (!title.empty())
            p.text(title, title_r, TextScale::Subtitle, pal.text, HAlign::Start, VAlign::Middle, false);

        // 吸管按钮：自绘一个斜柄 + 尾端挤压球，不依赖图标字体
        if (allow_dropper && pick_r.w > 0.f) {
            if (dropping_) p.fill_round(pick_r, Radius::control, pal.accent.alpha(0.20f));
            else if (hover_btn_ == 100) p.fill_round(pick_r, Radius::control, pal.flyout_hover);
            Color ic = dropping_ ? pal.accent : pal.text_secondary;
            Point a(pick_r.x + 6.f, pick_r.bottom() - 6.f);
            Point b(pick_r.right() - 7.f, pick_r.y + 7.f);
            p.line(a, b, ic, 4.f);
            p.fill_circle(a, 3.4f, ic);
            p.fill_round(Rect(b.x - 3.5f, b.y - 3.5f, 8.f, 8.f), 3.5f, ic);
        }

        // HSV 方块：纯色相打底 → 左白（饱和度）→ 下黑（明度）
        p.fill_rect(sv_r, Color::hex(hue_rgb()));
        p.fill_linear(sv_r, {{0.f, Color(1.f, 1.f, 1.f, 1.f)}, {1.f, Color(1.f, 1.f, 1.f, 0.f)}},
                      false);
        p.fill_linear(sv_r, {{0.f, Color(0.f, 0.f, 0.f, 0.f)}, {1.f, Color(0.f, 0.f, 0.f, 1.f)}},
                      true);
        p.stroke_round(sv_r.inset(-0.5f), 2.f, pal.flyout_border, 1.f);

        // 取色点：白圈 + 里层填当前色，一眼能看出"点在哪"
        {
            Point cp(sv_r.x + sat * sv_r.w, sv_r.y + (1.f - val) * sv_r.h);
            p.fill_circle(cp, 6.5f, Color(1.f, 1.f, 1.f, 1.f));
            p.fill_circle(cp, 4.5f, cur_color());
            p.arc(cp, 6.f, 0.f, 359.9f, 1.f, Color(0.f, 0.f, 0.f, 0.35f));
        }

        // 色相条（6 段 7 停靠点）+ 一个白色横杠标位置
        p.fill_linear(hue_r,
                      {{0.f, Color::hex(0xFF0000)}, {1.f / 6.f, Color::hex(0xFFFF00)},
                       {2.f / 6.f, Color::hex(0x00FF00)}, {3.f / 6.f, Color::hex(0x00FFFF)},
                       {4.f / 6.f, Color::hex(0x0000FF)}, {5.f / 6.f, Color::hex(0xFF00FF)},
                       {1.f, Color::hex(0xFF0000)}},
                      true);
        {
            float hy = hue_r.y + (hue / 360.f) * hue_r.h;
            Rect bar(hue_r.x - 2.f, hy - 1.5f, hue_r.w + 4.f, 3.f);
            p.fill_round(bar, 1.5f, Color(1.f, 1.f, 1.f, 1.f));
            p.stroke_round(bar.inset(0.5f), 1.5f, Color(0.f, 0.f, 0.f, 0.30f), 1.f);
        }

        // 当前值：预览块 + 按程序给的格式串渲染的文字
        p.fill_round(preview_r, Radius::control, cur_color());
        p.stroke_round(preview_r.inset(0.5f), Radius::control, pal.flyout_border, 1.f);
        p.text(format_color(r, g, b, color_format),
               Rect(preview_r.right() + 10.f, value_r.y, std::max(0.f, value_r.w - 32.f), value_h),
               TextScale::Body, pal.text, HAlign::Start, VAlign::Middle, false);

        // 预设色板（程序给几块就画几块，等分；当前色描一圈）
        if (!swatches.empty()) {
            for (std::size_t i = 0; i < swatches.size(); ++i) {
                Rect c(sw_r.x + (float)i * sw_cw + 1.f, sw_r.y, sw_cw - 2.f, sw_r.h);
                p.fill_round(c, Radius::control, Color::hex(swatches[i]));
                if ((int)(swatches[i] & 0xFFFFFFu) == color_rgb())
                    p.stroke_round(c.inset(0.5f), Radius::control, pal.text, 1.f);
                else
                    p.stroke_round(c.inset(0.5f), Radius::control, pal.flyout_border, 1.f);
            }
        }

        // 底栏：细线 + 取消（描边按钮）+ 确定（实心）
        if (!ok_text.empty() || !cancel_text.empty()) {
            p.fill_rect(Rect(bounds.x + pad, foot_y - 10.f, bounds.w - pad * 2.f, 1.f),
                        pal.flyout_sep);
            if (!cancel_text.empty()) {
                p.fill_round(cancel_r, Radius::control,
                             hover_btn_ == 4 ? pal.flyout_hover : pal.flyout);
                p.stroke_round(cancel_r.inset(0.5f), Radius::control, pal.flyout_border, 1.f);
                p.text(cancel_text, cancel_r, TextScale::Body, pal.text,
                       HAlign::Center, VAlign::Middle, false);
            }
            if (!ok_text.empty()) {
                p.fill_round(ok_r, Radius::control,
                             hover_btn_ == 3 ? pal.accent_hover : pal.accent);
                p.text(ok_text, ok_r, TextScale::Body, pal.on_accent,
                       HAlign::Center, VAlign::Middle, false);
            }
        }
    }

    virtual void mouse_down(Point pt) override {
        int h = hit(pt);
        if (h == 100) {
            // 按下只进入“拖着取色”状态，**不取样**。
            // 以前按下就 sample 一次、抬手再 sample 并 finish，
            // 所以“往吸管上点一下”就把按钮处的颜色填进去了。
            dropping_ = true;
            drop_from_ = pt;
            drop_moved_ = false;
            hover_btn_ = 100;
            mark_dirty();
            return;
        }
        if (h == 1 || h == 2) {          // 按在 SV / 色相条上：立刻跟手，之后拖着走
            drag_zone_ = h;
            drag_to(pt);
        }
    }
    virtual void mouse_move(Point pt) override {
        if (dropping_) {
            // 拖开 4px 才算“真的在取色”；只是点一下不算。
            // 之后不看 pt，直接查全局光标——拖到面板外也要跟手。
            if (!drop_moved_ && std::abs(pt.x - drop_from_.x) + std::abs(pt.y - drop_from_.y) > 4.f)
                drop_moved_ = true;
            if (drop_moved_) sample_cursor();
            return;
        }
        if (drag_zone_) { drag_to(pt); return; }
        int h = hit(pt);
        int hb = (h == 3 || h == 4) ? h : (h == 100 ? 100 : 0);
        if (hb != hover_btn_) { hover_btn_ = hb; mark_dirty(); }
    }
    virtual void mouse_leave() override {
        if (dropping_) return;           // 拖着吸管出面板是正常操作，别把状态清掉
        if (hover_btn_ != 0) { hover_btn_ = 0; mark_dirty(); }
    }

    virtual void click(Point pt) override {
        if (dropping_) {
            bool moved = drop_moved_;
            dropping_ = false;
            drop_moved_ = false;
            hover_btn_ = 0;
            mark_dirty();
            if (!moved) return;          // 没拖过 = 只是点了下吸管，什么都不发生
            sample_cursor();             // 松手这一刻的光标位置才是结果
            finish();
            return;
        }
        if (drag_zone_) { drag_zone_ = 0; return; }        // 刚拖完松手，不算一次点击
        int h = hit(pt);
        if (h == 3) { finish(); return; }                   // 确定
        if (h == 4) { if (on_close_request) on_close_request(); return; }   // 取消
        if (h >= 5) {                                       // 点色板：立刻回填
            unsigned v = swatches[(std::size_t)(h - 5)];
            set_rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
            finish();
            return;
        }
        if (h == 1 || h == 2) { drag_zone_ = h; drag_to(pt); }
    }

    void finish() {
        drag_zone_ = 0;
        if (on_pick_color) on_pick_color(r, g, b);
    }

    // ---- 命中矩形（自测/程序想自己贴东西都能读）----
    Rect sv_rect()      const { return sv_r; }
    Rect hue_rect()     const { return hue_r; }
    Rect dropper_rect() const { return pick_r; }
    bool dropper_active() const { return dropping_; }
    Rect ok_btn_rect()   const { return ok_r; }
    Rect cancel_btn_rect() const { return cancel_r; }
    Rect swatch_rect(int i) const {
        return Rect(sw_r.x + (float)i * sw_cw + 1.f, sw_r.y, sw_cw - 2.f, sw_r.h);
    }

private:
    Color cur_color() const {
        return Color((float)r / 255.f, (float)g / 255.f, (float)b / 255.f, 1.f);
    }
    // 吸管：读屏幕上一个像素。用全局光标位置，这样拖到面板外也照样跟手。
    void sample_cursor() {
        POINT sp{};
        if (!GetCursorPos(&sp)) return;
        HDC dc = GetDC(nullptr);
        if (!dc) return;
        COLORREF cr = GetPixel(dc, sp.x, sp.y);
        ReleaseDC(nullptr, dc);
        if (cr == CLR_INVALID) return;
        set_rgb((int)(cr & 0xFF), (int)((cr >> 8) & 0xFF), (int)((cr >> 16) & 0xFF));
    }
    unsigned hue_rgb() const {
        int rr = 0, gg = 0, bb = 0;
        hsv_to_rgb(hue, 1.f, 1.f, rr, gg, bb);
        return ((unsigned)rr << 16) | ((unsigned)gg << 8) | (unsigned)bb;
    }
    void sync_rgb() { hsv_to_rgb(hue, sat, val, r, g, b); }

    Rect title_r, sv_r, hue_r, value_r, preview_r, sw_r, ok_r, cancel_r;
    float foot_y = 0.f, sw_cw = 0.f;
    int   hover_btn_ = 0;
    int   drag_zone_ = 0;     // 0 无，1 SV，2 色相条
};

// ------------------------------------------------------------ CalendarView ----
// 月历视图：标题（可切月）+ 星期表头 + 6×7 日格。星期名、月份标题格式都由程序给。
class CalendarView : public Widget {
public:
    int year = 0, month = 0;                 // 0 = 用今天
    int today_y = 0, today_m = 0, today_d = 0;   // 0 = 不标今天
    int sel_y = 0, sel_m = 0, sel_d = 0;         // 0 = 无选中
    std::vector<std::wstring> week;          // 7 个表头，空 = 不画表头
    std::wstring month_format = L"{y}-{m}";  // 标题格式，程序传
    float cell_w = 40.f, cell_h = 34.f, head_h = 36.f, pad = 8.f;
    std::wstring glyph_prev = L"\uE76B", glyph_next = L"\uE76C";
    std::function<void(int,int,int)> on_pick;

    CalendarView() {}

    template <class... Args>
    explicit CalendarView(Args&&... args) : CalendarView() {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        if (!o.items.empty()) week = o.items;
        if (!o.time_format.empty()) month_format = o.time_format;
        if (!std::isnan(o.width)) cell_w = o.width;
        if (!std::isnan(o.height)) cell_h = o.height;
        sel_y = o.year; sel_m = o.month; sel_d = o.day;
        flex = o.flex;
    }

    void set_month(int y, int m, bool animate = true) {
        while (m < 1) { m += 12; --y; }
        while (m > 12) { m -= 12; ++y; }
        if (y == year && m == month) return;
        if (animate && slide_enabled && year > 0) {   // 首次设置（year==0）不做动画，否则没东西可滑
            // 切月时整块日期网格上下滚：往后的月份旧网格向上滑出、新网格从下面进来，
            // 往前反过来。表头（星期名）不动，只有日期区在滚。
            slide_from_y_ = year; slide_from_m_ = month;
            slide_dir_ = (y * 12 + m) > (year * 12 + month) ? 1 : -1;
            slide_t0_ = now_ms();
        } else {
            slide_t0_ = -1.0; slide_dir_ = 0;
        }
        year = y; month = m;
        if (host) { host->mark_animating(); host->request_repaint(); }
    }
    void set_selected(int y, int m, int d) {
        sel_y = y; sel_m = m; sel_d = d;
        if (host) host->request_repaint();
    }

    // 切月滚动的状态（程序一般不用碰）
    int    slide_dir_    = 0;        // +1 去下个月、-1 去上个月、0 静止
    double slide_t0_     = -1.0;
    double slide_ms      = 220.0;
    int    slide_from_y_ = 0, slide_from_m_ = 0;
    bool   slide_enabled = true;     // 程序想关就置 false（批量换月时不闪）
    float  pin_slide_e   = -1.f;     // 出图用：>0 时把滚动钉在这个进度，不看时间

    // 出图用：把"从 from_y/from_m 滚到当前月"的过程钉在 e（0..1）
    void pin_slide(float e, int from_y, int from_m) {
        slide_from_y_ = from_y; slide_from_m_ = from_m;
        slide_dir_ = 1; slide_t0_ = -1.0;
        pin_slide_e = std::clamp(e, 0.f, 1.f);
        if (host) host->request_repaint();
    }

    float grid_w() const { return cell_w * 7.f; }
    float gx() const {
        return bounds.x + std::max(pad, (bounds.w - grid_w()) * 0.5f);   // 网格水平居中
    }
    void resolved(int& y, int& m) const {
        y = year > 0 ? year : 2026;
        m = (month >= 1 && month <= 12) ? month : 1;
    }
    int rows() const {
        int y = 0, m = 1;
        resolved(y, m);
        int n = weekday_of(y, m, 1) + days_in_month(y, m);
        return (n + 6) / 7;
    }

    Size measure(Painter& p, Size avail) override {
        (void)p; (void)avail;
        float h = head_h + (week.empty() ? 0.f : 20.f) + cell_h * (float)rows() + pad * 2.f;
        return Size(grid_w() + pad * 2.f, h);
    }

    void arrange(Painter& p, const Rect& r) override { bounds = r; }

    // 第 i 行第 c 列（0..rows-1 / 0..6）的格子矩形
    Rect cell_rect(int i, int c) const {
        float top = bounds.y + pad + head_h + (week.empty() ? 0.f : 20.f);
        return Rect(gx() + cell_w * (float)c, top + cell_h * (float)i, cell_w, cell_h);
    }

    int day_at(Point pt, int& row, int& col) const {
        row = col = -1;
        for (int i = 0; i < rows(); ++i)
            for (int c = 0; c < 7; ++c)
                if (cell_rect(i, c).contains(pt)) { row = i; col = c; return (i * 7 + c); }
        return -1;
    }

    Widget* hit_test(Point pt) override { return bounds.contains(pt) ? this : nullptr; }

    void on_click_at(Point pt) override {
        int r = 0, c = 0;
        if (day_at(pt, r, c) >= 0) {
            int d = day_index_at(r, c);
            if (d < 1) return;
            sel_y = year; sel_m = month; sel_d = d;
            if (on_pick) on_pick(year, month, d);
            if (host) { host->request_repaint(); if (!change_name.empty()) host->fire(change_name, this, true); }
            return;
        }
        // 标题两侧的切月箭头：命中区必须和真正画出来的那两个 glyph 对齐
        // （网格是水平居中的，不能拿 bounds.right() 当箭头位置，否则点了没反应）
        if (pt.y >= bounds.y + pad && pt.y <= bounds.y + pad + head_h) {
            Rect prev_btn(gx() - 4.f, bounds.y + pad, 24.f, head_h);
            Rect next_btn(gx() + grid_w() - 20.f, bounds.y + pad, 24.f, head_h);
            if (prev_btn.contains(pt)) { set_month(year, month - 1); return; }
            if (next_btn.contains(pt)) { set_month(year, month + 1); return; }
        }
    }

    int day_index_at(int row, int col) const {
        int wd1 = weekday_of(year, month, 1);          // 0 = 周日
        int day = row * 7 + col - wd1 + 1;
        if (day < 1 || day > days_in_month(year, month)) return 0;
        return day;
    }

    std::wstring title_text() const {
        std::wstring s = month_format;
        auto rep = [&](const wchar_t* key, int val, int width) {
            std::size_t pos = s.find(key);
            if (pos == std::wstring::npos) return;
            std::wstring v = std::to_wstring(val);
            if (width > 1) {
                std::wstring t;
                for (int k = (int)v.size(); k < width; ++k) t += L'0';
                v = t + v;
            }
            s.replace(pos, std::wcslen(key), v);
        };
        rep(L"{y}", year, 4);
        rep(L"{m}", month, 2);
        rep(L"{d}", 1, 1);
        return s;
    }

    // 某年某月要几行（5 或 6）
    int rows_of(int yy, int mm) const {
        int n = weekday_of(yy, mm, 1) + days_in_month(yy, mm);
        return (n + 6) / 7;
    }
    // 某年某月第 row 行第 col 列是几号（0 = 不在本月）
    int day_of(int yy, int mm, int row, int col) const {
        int wd1 = weekday_of(yy, mm, 1);
        int day = row * 7 + col - wd1 + 1;
        if (day < 1 || day > days_in_month(yy, mm)) return 0;
        return day;
    }

    // 只画日期网格（表头以下），dy 是整体竖直偏移 —— 切月滚动时同一段画两遍
    void paint_grid(Painter& p, int yy, int mm, float dy) {
        const Palette& pal = p.palette();
        int wd1 = weekday_of(yy, mm, 1);
        int dim = days_in_month(yy, mm);
        int pm = mm - 1, py = yy;
        if (pm < 1) { pm = 12; --py; }
        int prev_dim = days_in_month(py, pm);
        int rows = rows_of(yy, mm);
        for (int i = 0; i < rows; ++i) {
            for (int c = 0; c < 7; ++c) {
                Rect cr = cell_rect(i, c);
                cr.y += dy;
                int day = day_of(yy, mm, i, c);
                bool in_month = day > 0;
                int shown = day;
                if (!in_month) {
                    int idx = i * 7 + c;
                    shown = idx < wd1 ? prev_dim - (wd1 - idx - 1) : (idx - wd1 - dim + 1);
                }
                bool is_today = today_y == yy && today_m == mm && today_d == shown && in_month;
                bool is_sel = sel_y == yy && sel_m == mm && sel_d == shown && in_month;
                if (is_sel) {
                    Rect hl = cr.inset(3.f);
                    p.fill_round(hl, Radius::control, pal.accent);
                } else if (is_today) {
                    Rect hl = cr.inset(3.f);
                    p.stroke_round(hl, Radius::control, pal.accent, 1.f);
                }
                Color cc = is_sel ? pal.on_accent : (in_month ? pal.text : pal.text_tertiary.alpha(0.5f));
                std::wstring ds = std::to_wstring(shown);
                p.text(ds, cr, TextScale::Caption, cc, HAlign::Center, VAlign::Middle, false);
            }
        }
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        int y = 0, m = 1;
        resolved(y, m);

        // 标题行
        p.text(title_text(), Rect(gx() + 24.f, bounds.y + pad, grid_w() - 48.f, head_h),
               TextScale::BodyStrong, pal.text, HAlign::Center, VAlign::Middle, false);
        float cy = bounds.y + pad + head_h * 0.5f;
        p.glyph(glyph_prev, Rect(gx() + 4.f, cy - 8.f, 16.f, 16.f), 11.f, pal.text_secondary);
        p.glyph(glyph_next, Rect(gx() + grid_w() - 20.f, cy - 8.f, 16.f, 16.f), 11.f,
                pal.text_secondary);

        if (!week.empty()) {
            float wy = bounds.y + pad + head_h;
            for (int c = 0; c < 7 && c < (int)week.size(); ++c)
                p.text(week[(size_t)c], Rect(gx() + cell_w * (float)c, wy, cell_w, 20.f),
                       TextScale::Caption, pal.text_tertiary, HAlign::Center, VAlign::Middle, false);
        }

        // 日期区（表头以下）——切月时这里做上下滚动
        float top = bounds.y + pad + head_h + (week.empty() ? 0.f : 20.f);
        Rect view(gx(), top, grid_w(), cell_h * (float)rows());

        bool sliding = false;
        float e = 1.f;
        if (pin_slide_e >= 0.f) {
            sliding = true;
            e = pin_slide_e;
        } else if (slide_dir_ != 0 && slide_t0_ >= 0.0) {
            float k = (float)((now_ms() - slide_t0_) / (slide_ms > 0.0 ? slide_ms : 1.0));
            if (k >= 1.f) { k = 1.f; slide_t0_ = -1.0; slide_dir_ = 0; }
            else sliding = true;
            e = ease_out_cubic(k);
        }

        if (sliding) {
            if (host) host->mark_animating();     // 让主循环继续出帧
            float vh = view.h;
            p.push_clip(view);
            if (slide_dir_ > 0) {                 // 去下个月：旧月往上走、新月从下面进
                paint_grid(p, slide_from_y_, slide_from_m_, -e * vh);
                paint_grid(p, y, m, (1.f - e) * vh);
            } else {                              // 回上个月：反过来
                paint_grid(p, slide_from_y_, slide_from_m_, e * vh);
                paint_grid(p, y, m, -(1.f - e) * vh);
            }
            p.pop_clip();
        } else {
            paint_grid(p, y, m, 0.f);
        }
    }
};

// ------------------------------------------------------------ NumberPad ----
// 数字小键盘：0-9 / 小数点 / 退格的九宫格（默认 3 列 4 行）。
// **唤起是程序的事**：程序 `app.popup(numpad)` 或把它塞进任何容器；
// 键位、标题、按钮字全部由程序给，库里只有"数字符号"这一套默认。
// 绑 `target` 就直接改那个输入框的文本；不给 target 就只走回调，程序自己写变量。
class NumberPad : public PopupPanel {
public:
    std::vector<std::wstring> keys;   // 空 = 默认 7 8 9 / 4 5 6 / 1 2 3 / 0 . ⌫
    int   cols = 3;
    float key_h = 40.f;               // 每个键高
    float gap   = 6.f;                // 键间距
    float pad   = 12.f;               // 面板内边距
    float panel_w = 240.f;
    float head_h  = 28.f;             // 标题行高（title 空则不占）
    float title_gap = 4.f;            // 标题与第一行键之间再留一点距离
    float foot_h  = 34.f;

    std::wstring title;                     // 空 = 不画标题
    std::wstring ok_text, cancel_text;      // 空 = 不画那个按钮
    std::wstring glyph_back = L"\b";        // 退格键：空串或 "\b" = 自绘退格形状；给图标码就画字形

    // 小数点键能不能用。整数框（TextBox::num_integer）配这个键盘时置 false：
    // 键画成灰的、点了也不出字，跟"这个框不接受小数"这件事对上。
    bool dot_enabled = true;

    // 库内部标记：这层弹层该由谁关。Window::attach_numpad 会挂一个 close_popup 进来，
    // 点确定 / 取消时先跑程序自己的回调、再调它关门。这样和程序"什么时候设 on_ok"无关
    // （程序在 attach 之后覆盖 on_ok 也不会把关门这一步弄丢）。
    std::function<void()> auto_close;

    std::function<void(const std::wstring&)> on_key;   // 每按一个可见键
    std::function<void()> on_backspace;                // 按退格
    std::function<void()> on_ok;                       // 点确定
    std::function<void()> on_close_request;            // 取消 / 关面板

    // 绑一个输入框：按键就直接往它里面写（不给就只走回调）
    std::shared_ptr<TextBox> target;

    // 自测/程序读：当前按在第几个键（-1 = 没按）
    int pressed_key_ = -1;

    NumberPad() { cast_shadow = true; radius = Radius::flyout; }

    template <class... Args>
    explicit NumberPad(Args&&... args) : NumberPad() {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        if (!o.title.empty()) title = o.title;
        ok_text = o.ok_text;
        cancel_text = o.cancel_text;
        if (!std::isnan(o.width) && o.width > 120.f) panel_w = o.width;
        if (!o.items.empty()) keys = o.items;
    }

    bool interactive() const override { return enabled; }

    float preferred_width() const { return panel_w; }

    Size measure(Painter& p, Size avail) override {
        (void)p;
        (void)avail;
        return Size(or_default(hint_w, panel_w), or_default(hint_h, preferred_height()));
    }

    float preferred_height() const {
        auto v = key_list();
        int cc = cols > 0 ? cols : 1;
        int rows = ((int)v.size() + cc - 1) / cc;
        if (rows < 1) rows = 1;
        float h = pad * 2.f + (float)rows * key_h + (float)(rows - 1) * gap;
        if (!title.empty()) h += head_h + title_gap;
        if (!ok_text.empty() || !cancel_text.empty()) h += 10.f + foot_h;
        return h;
    }

    // 程序一个键都没给时的默认布局：全是符号，没有文字
    std::vector<std::wstring> key_list() const {
        if (!keys.empty()) return keys;
        return {L"7", L"8", L"9",
                L"4", L"5", L"6",
                L"1", L"2", L"3",
                L"0", L".", glyph_back};
    }
    bool is_back_at(int i) const {
        auto v = key_list();
        if (i < 0 || i >= (int)v.size()) return false;
        const std::wstring& k = v[(std::size_t)i];
        return k == L"\b" || k == glyph_back || k.empty();
    }
    bool is_dot_at(int i) const {
        auto v = key_list();
        if (i < 0 || i >= (int)v.size()) return false;
        return v[(std::size_t)i] == L".";
    }
    // 这个键此刻能不能按（小数点键在整数模式下不能）
    bool is_key_live(int i) const { return !(is_dot_at(i) && !dot_enabled); }

    void arrange(Painter& p, const Rect& r) override {
        (void)p;
        // 内嵌在卡片里时父容器可能给得比面板宽，收回到 panel_w 并居中；
        // 当弹层用的时候 r 本来就是 preferred 尺寸，这一步不改变任何东西。
        float w = std::min(r.w, panel_w);
        float h = std::min(r.h > 0.f ? r.h : preferred_height(), preferred_height());
        bounds = Rect(r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h);

        float y = bounds.y + pad;
        if (!title.empty()) { title_r = Rect(bounds.x + pad, y, w - pad * 2.f, head_h); y += head_h + title_gap; }
        else                { title_r = Rect(); }

        auto v = key_list();
        int cc = cols > 0 ? cols : 1;
        cell_w = (w - pad * 2.f - gap * (float)(cc - 1)) / (float)cc;
        grid_y = y;

        if (!ok_text.empty() || !cancel_text.empty()) {
            foot_y = bounds.y + h - pad - foot_h;
            float bw = 84.f;
            cancel_r = Rect(bounds.right() - pad - bw * 2.f - 10.f, foot_y, bw, foot_h);
            ok_r     = Rect(bounds.right() - pad - bw,               foot_y, bw, foot_h);
        } else {
            foot_y = bounds.y + h;
            ok_r = cancel_r = Rect();
        }
    }

    Rect key_rect(int i) const {
        int cc = cols > 0 ? cols : 1;
        int c = i % cc, rr = i / cc;
        float x = bounds.x + pad + (float)c * (cell_w + gap);
        float y = grid_y + (float)rr * (key_h + gap);
        return Rect(x, y, cell_w, key_h);
    }

    // 命中：-1 无，0..n-1 键，100 确定，101 取消
    int hit(Point pt) const {
        auto v = key_list();
        for (int i = 0; i < (int)v.size(); ++i)
            if (key_rect(i).contains(pt)) return i;
        if (!ok_text.empty() && ok_r.contains(pt)) return 100;
        if (!cancel_text.empty() && cancel_r.contains(pt)) return 101;
        return -1;
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        draw_shadow(p);
        p.fill_round(bounds, radius, pal.flyout);
        p.stroke_round(bounds.inset(0.5f), radius, pal.flyout_border, 1.f);

        if (!title.empty())
            p.text(title, title_r, TextScale::Subtitle, pal.text, HAlign::Start, VAlign::Middle, false);

        auto v = key_list();
        for (int i = 0; i < (int)v.size(); ++i) {
            Rect kr = key_rect(i);
            bool dim = is_dot_at(i) && !dot_enabled;
            Color face = pal.flyout_hover;
            if (hover_ == i && !dim) face = pal.dark ? Color(1.f, 1.f, 1.f, 0.10f) : Color(0.f, 0.f, 0.f, 0.06f);
            if (pressed_key_ == i) face = pal.accent.alpha(0.22f);
            p.fill_round(kr, Radius::control, face);
            if (is_back_at(i)) {
                Rect ib(kr.x + kr.w * 0.5f - 10.f, kr.y + kr.h * 0.5f - 7.f, 20.f, 14.f);
                if (glyph_back.empty() || glyph_back == L"\b") draw_back_icon(p, ib, pal.text);
                else p.glyph(glyph_back, ib, 13.f, pal.text);
            } else {
                p.text(v[(std::size_t)i], kr, TextScale::BodyStrong,
                       dim ? pal.text.alpha(0.32f) : pal.text,
                       HAlign::Center, VAlign::Middle, false);
            }
        }

        if (!ok_text.empty() || !cancel_text.empty()) {
            // 按钮区上面不画分隔线：面板本身就是一块，多一根线反而显脏
            if (!cancel_text.empty()) {
                p.fill_round(cancel_r, Radius::control, hover_btn_ == 4 ? pal.flyout_hover : pal.flyout);
                p.stroke_round(cancel_r.inset(0.5f), Radius::control, pal.flyout_border, 1.f);
                p.text(cancel_text, cancel_r, TextScale::Body, pal.text, HAlign::Center, VAlign::Middle, false);
            }
            if (!ok_text.empty()) {
                p.fill_round(ok_r, Radius::control, hover_btn_ == 3 ? pal.accent_hover : pal.accent);
                p.text(ok_text, ok_r, TextScale::Body, pal.on_accent, HAlign::Center, VAlign::Middle, false);
            }
        }
    }

    // 不同宿主走两条派发路线：嵌在窗口里走 Scene 的 on_press / on_click_at，
    // 当弹层用时走弹层窗口自己的 mouse_down / click。两边都要能用。
    void on_press(bool on) override {
        if (!on) { pressed_key_ = -1; if (host) host->request_repaint(); }
    }
    void on_click_at(Point pt) override { click(pt); }

    void mouse_down(Point pt) override {
        int h = hit(pt);
        if (h >= 0 && h < 100 && is_key_live(h)) { pressed_key_ = h; mark_dirty(); }
    }
    void mouse_move(Point pt) override {
        int h = hit(pt);
        int hv = (h >= 0 && h < 100 && is_key_live(h)) ? h : -1;
        int hb = (h == 100) ? 3 : ((h == 101) ? 4 : 0);
        if (hv != hover_ || hb != hover_btn_) { hover_ = hv; hover_btn_ = hb; mark_dirty(); }
    }
    void mouse_leave() override {
        if (hover_ != -1 || hover_btn_ != 0) { hover_ = -1; hover_btn_ = 0; mark_dirty(); }
    }

    void click(Point pt) override {
        int h = hit(pt);
        pressed_key_ = -1;
        if (h >= 0 && h < 100) { fire_key(h); return; }        if (h == 100) { if (on_ok) on_ok(); if (auto_close) auto_close(); return; }
        if (h == 101) { if (on_close_request) on_close_request(); if (auto_close) auto_close(); return; }
    }

    // 程序也能自己按某个键（键盘 / 快捷键映射）
    void fire_key(int i) {
        auto v = key_list();
        if (i < 0 || i >= (int)v.size()) return;
        if (!is_key_live(i)) return;                 // 整数模式下的小数点键：按了没反应
        if (is_back_at(i)) {
            if (target) target->on_key(VK_BACK, false, false);
            if (on_backspace) on_backspace();
        } else {
            const std::wstring& k = v[(std::size_t)i];
            if (target) for (wchar_t ch : k) target->on_char(ch);
            if (on_key) on_key(k);
        }
        mark_dirty();
    }

    // ---- 自测 / 程序读的几何 ----
    Rect key_box(int i) const { return key_rect(i); }
    Rect ok_btn_rect() const { return ok_r; }
    Rect cancel_btn_rect() const { return cancel_r; }
    int  key_count() const { return (int)key_list().size(); }

private:
    // 退格图标自绘（不靠字体里有没有那个字形）：左尖的框 + 里面一个 ×
    static void draw_back_icon(Painter& p, Rect box, Color c) {
        float mid = box.y + box.h * 0.5f;
        float notch = box.x + box.w * 0.30f;
        float top = box.y + 1.f, bot = box.bottom() - 1.f, rt = box.right() - 1.f;
        p.line(Point(box.x, mid),     Point(notch, top), c, 1.6f);
        p.line(Point(notch, top),     Point(rt, top),    c, 1.6f);
        p.line(Point(rt, top),        Point(rt, bot),    c, 1.6f);
        p.line(Point(rt, bot),        Point(notch, bot), c, 1.6f);
        p.line(Point(notch, bot),     Point(box.x, mid), c, 1.6f);
        float cx = box.x + box.w * 0.63f, cy = mid, r = 2.8f;
        p.line(Point(cx - r, cy - r), Point(cx + r, cy + r), c, 1.6f);
        p.line(Point(cx - r, cy + r), Point(cx + r, cy - r), c, 1.6f);
    }

    Rect  title_r, ok_r, cancel_r;
    float cell_w = 0.f, grid_y = 0.f, foot_y = 0.f;
    int   hover_ = -1;
    int   hover_btn_ = 0;
};

namespace ui {

template <class... Args>
std::shared_ptr<DatePicker> datepicker(Args&&... args) {
    return std::make_shared<DatePicker>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<ColorPicker> colorpicker(Args&&... args) {
    return std::make_shared<ColorPicker>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<CalendarView> calendar(Args&&... args) {
    return std::make_shared<CalendarView>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<NumberPad> numpad(Args&&... args) {
    return std::make_shared<NumberPad>(std::forward<Args>(args)...);
}

}  // namespace ui

}  // namespace w11ui
