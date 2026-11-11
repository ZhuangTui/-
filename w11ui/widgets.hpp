// w11ui / widgets.hpp ---------------------------------------------------------
// 控件树：Label / Button / Toggle / ProgressRing / Divider / Spacer / Stack / Card
// 每个控件都是 Widget 的子类，用同一套 kw:: 选项构造。
#pragma once

#include "core.hpp"
#include "theme.hpp"
#include "kwargs.hpp"
#include "painter.hpp"

namespace w11ui {

class Widget;
class Window;
class PopupPanel;
enum class ButtonKind { Standard, Toggle, Repeat, Hyperlink, Split, DropDown };

struct Event {
    std::string name;
    Widget*     widget = nullptr;
    Window*     window = nullptr;
    bool        checked = false;   // 开关类控件的最新状态
};

// 弹出层（下拉框列表）相对触发控件的展开方向
enum class PopupDir { Down, Up, Right, Left };

// 控件通过这个接口回调宿主（Scene），避免控件层反向依赖 Scene/Window。
struct Host {
    virtual ~Host() = default;
    virtual void request_repaint() = 0;
    virtual void mark_animating() = 0;
    virtual void fire(const std::string& name, Widget* w, bool checked) = 0;
    virtual void want_hand_cursor(bool on) = 0;
    // 控件请求键盘焦点（点输入框时用）
    virtual void want_focus(Widget* w) { (void)w; }
    // 模态层：Dialog 自己往这一层挂
    virtual void open_modal(std::shared_ptr<Widget> layer) { (void)layer; }
    virtual void close_modal() {}

    // ---- 弹出层支撑（默认实现 = 不支持弹层，离屏渲染时安全降级）----
    virtual Rect  client_rect() const { return Rect(0.f, 0.f, 900.f, 600.f); }          // 逻辑像素
    virtual Rect  client_screen_rect() const { return Rect(0.f, 0.f, 900.f, 600.f); }   // 物理像素
    virtual Rect  monitor_work_rect() const { return Rect(0.f, 0.f, 1920.f, 1080.f); }  // 物理像素
    virtual float dpi_scale() const { return 1.f; }
    // 显示弹出层：screen_px 是面板最终的屏幕矩形（物理像素），dir 决定展开动画的锚边
    virtual bool  show_popup(std::shared_ptr<PopupPanel> panel, const Rect& screen_px, PopupDir dir) {
        (void)panel; (void)screen_px; (void)dir;
        return false;
    }
    virtual void  close_popup() {}
    virtual bool  popup_visible() const { return false; }
    // 从当前最深一层菜单的第 row 行，再拉出一层子菜单（层数不限）
    virtual bool  open_submenu(std::shared_ptr<PopupPanel> panel, int row) {
        (void)panel; (void)row;
        return false;
    }
    // 面板要点开自己的子菜单之前，先报一下"我是第几层"，好让子菜单开在我右边
    virtual void  note_submenu_source(int level) { (void)level; }
    // 这一套下拉/菜单的默认"悬浮展开"行为（从最外层下拉传进来，子菜单跟着走）
    virtual void  set_popup_hover(bool on, double delay_ms) { (void)on; (void)delay_ms; }
};

inline float or_default(float v, float d) { return std::isnan(v) ? d : v; }
inline bool  shadow_opt(const Opts& o, bool def) { return o.shadow < 0 ? def : o.shadow > 0; }
inline int& mm_counter() { static int c = 0; return c; }   // 自测：mouse_move 被调了几次

inline Color state_color(Color base, Color hover, Color press, float hv, float pv) {
    return mix(mix(base, hover, hv), press, pv);
}

// --------------------------------------------------------------- Widget ----
class Widget : public std::enable_shared_from_this<Widget> {
public:
    virtual ~Widget() = default;

    // 布局提示
    float hint_w = NAN;
    float hint_h = NAN;
    float flex   = 0.f;

    bool enabled = true;
    Rect bounds;

    Host* host = nullptr;
    bool  hovered = false;
    bool  pressed = false;

    std::string click_name;
    std::string change_name;

    // 右键菜单：kw::context() + kw::items({...})。空 = 该控件没有右键菜单。
    std::vector<std::wstring> context_items;
    std::function<void(int)> on_context_pick;

    bool cast_shadow = false;  // 默认不投影；kw::shadow(true) 打开（卡片/下拉框默认开）

    // 阴影形状/强度由控件自己说了算（大卡片大阴影、开关只有一点点）
    virtual float shadow_radius() const { return 8.f; }
    virtual float shadow_blur() const { return 4.5f; }
    virtual float shadow_offset() const { return 2.5f; }
    // 在 paint() 开头调用：cast_shadow 为假时什么都不画
    void draw_shadow(Painter& p) {
        if (!cast_shadow) return;
        const Palette& pal = p.palette();
        p.shadow(bounds, shadow_radius(), shadow_blur(), pal.shadow, shadow_offset());
    }

    Tween hover_t;
    Tween press_t;

    virtual bool interactive() const { return enabled && (!click_name.empty() || !change_name.empty()); }
    virtual bool hand_cursor() const { return interactive(); }
    virtual bool checked_state() const { return false; }

    virtual Size measure(Painter& p, Size avail) {
        (void)p;
        return Size(or_default(hint_w, 0.f), or_default(hint_h, 0.f));
    }
    virtual void arrange(Painter& p, const Rect& r) {
        (void)p;
        bounds = r;
    }
    virtual void paint(Painter& p) { (void)p; }

    virtual void on_hover(bool on) {
        hovered = on;
        hover_t.go(on ? 1.f : 0.f, now_ms(), Motion::fast);
    }
    virtual void on_press(bool on) {
        pressed = on;
        press_t.go(on ? 1.f : 0.f, now_ms(), Motion::fast);
    }
    virtual void on_click() {
        if (host && !click_name.empty()) host->fire(click_name, this, checked_state());
    }
    // 带坐标的点击（输入框要靠它定位光标），默认转给 on_click
    virtual void on_click_at(Point pt) { (void)pt; on_click(); }
    // 按住拖动（拖选文字），默认不做
    virtual void on_drag(Point pt) { (void)pt; }
    // 鼠标在控件上移动（已带坐标，弹窗靠它判断按钮 hover）
    virtual void on_move(Point pt) { (void)pt; }

    // ---- 键盘：只有"要键盘"的控件（输入框）会被 Scene 分到按键 ----
    virtual bool wants_key() const { return false; }
    virtual bool on_key(int vk, bool ctrl, bool shift) { (void)vk; (void)ctrl; (void)shift; return false; }
    virtual void on_char(wchar_t ch) { (void)ch; }
    virtual void on_focus(bool on) { (void)on; }
    virtual bool text_cursor() const { return false; }   // 鼠标移上去变 I 型

    // 命中测试：默认只有自身可交互才返回自己
    virtual Widget* hit_test(Point pt) {
        if (interactive() && bounds.contains(pt)) return this;
        return nullptr;
    }

    // 右键菜单：在自己身上弹出 context_items（定义在 PopupPanel 之后）
    void open_context_menu(Point pt);

    void attach(Host* h) {
        host = h;
        on_attach();
    }
    virtual void on_attach() {}

    void set_enabled(bool v) {
        enabled = v;
        if (host) host->request_repaint();
    }

protected:
    // 每帧推进动画；还在动就告诉宿主需要下一帧
    void tick_anim() {
        double t = now_ms();
        bool running = hover_t.active(t) | press_t.active(t);
        if (running && host) host->mark_animating();
    }
};

// ---------------------------------------------------------------- Label ----
class Label : public Widget {
public:
    std::wstring text;
    TextScale scale = TextScale::Body;
    Role role = Role::Primary;
    HAlign align = HAlign::Start;
    bool wrap = false;
    bool display_font = false;

    template <class... Args>
    explicit Label(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        text = o.text.empty() ? o.title : o.text;
        scale = o.scale;
        role = o.role;
        align = o.align;
        wrap = o.wrap;
        hint_w = o.width;
        hint_h = o.height;
        enabled = o.enabled;
    }

    void set_text(const std::wstring& t) {        text = t;
        if (host) host->request_repaint();
    }
    void set_text(const std::string& t) { set_text(to_wide(t)); }

    Size measure(Painter& p, Size avail) override {
        float maxw = std::isnan(hint_w) ? avail.w : hint_w;
        Size s = p.measure_text(text, scale, wrap, maxw, display_font);
        float w = wrap ? maxw : (std::isnan(hint_w) ? s.w : hint_w);
        float h = or_default(hint_h, s.h);
        return Size(w, h);
    }

    void paint(Painter& p) override {
        tick_anim();
        Color c = p.palette().text_for(enabled ? role : Role::Disabled);
        p.text(text, bounds, scale, c, align, VAlign::Middle, wrap, display_font);
    }
};

// ----------------------------------------------------------------- Image ----
// 位图控件：给一个 png/jpg/bmp 的路径，按 cover（裁满，默认）或 contain（完整装入）画。
// 加载失败只画一块中性占位，不写任何文字（库不带固定内容）。
class Image : public Widget {
public:
    std::wstring src;
    bool  cover = true;
    float opacity = 1.f;
    float radius = Radius::card;
    bool  loaded = false;      // 上一次 paint 是否真的解到图（自测用）

    template <class... Args>
    explicit Image(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        src = o.src;
        cover = (o.fit_mode != 1);
        hint_w = o.width;
        hint_h = o.height;
        flex = o.flex;
        enabled = o.enabled;
        radius = or_default(o.radius, Radius::card);
    }

    Size measure(Painter& p, Size avail) override {
        (void)p;
        return Size(or_default(hint_w, 160.f), or_default(hint_h, 100.f));
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        ID2D1Bitmap* bmp = p.load_bitmap(src);
        if (!bmp) {
            p.fill_round(bounds, radius, pal.control.alpha(pal.dark ? 0.30f : 0.55f));
            return;
        }
        Size bs = p.bitmap_size(bmp);
        if (bs.w <= 0.f || bs.h <= 0.f || bounds.w <= 0.f || bounds.h <= 0.f) return;

        p.push_clip(bounds);
        loaded = true;
        if (cover) {
            // 取中间最大等比区域，填满整个控件（不会变形）
            float ar_dst = bounds.w / bounds.h;
            float ar_src = bs.w / bs.h;
            Rect s(0.f, 0.f, bs.w, bs.h);
            if (ar_src > ar_dst) {
                s.w = bs.h * ar_dst;
                s.x = (bs.w - s.w) * 0.5f;
            } else {
                s.h = bs.w / ar_dst;
                s.y = (bs.h - s.h) * 0.5f;
            }
            p.draw_bitmap_src(bmp, bounds, s, opacity);
        } else {
            Rect d = bounds;
            float ar_src = bs.w / bs.h;
            if (bounds.w / bounds.h > ar_src) {
                d.w = bounds.h * ar_src;
                d.x = bounds.x + (bounds.w - d.w) * 0.5f;
            } else {
                d.h = bounds.w / ar_src;
                d.y = bounds.y + (bounds.h - d.h) * 0.5f;
            }
            p.draw_bitmap(bmp, d, opacity);
        }
        p.pop_clip();
    }
};

// --------------------------------------------------------------- Button ----
// 一个类覆盖整族按钮：普通 / 切换 / 重复 / 超链接 / 拆分 / 下拉。
// 形态由 kw::as_toggle / as_repeat / as_hyperlink / as_split / as_dropdown 指定，
// 默认仍是普通按钮（观感不变）。
class Button : public Widget {
public:
    std::wstring text;
    std::wstring icon;
    Variant variant = Variant::Standard;
    ButtonKind kind = ButtonKind::Standard;

    bool   check_on = false;        // 切换按钮的当前按下状态
    long   repeat_delay = 400;      // 重复按钮：按住多久开始连发
    long   repeat_every = 60;
    float  arrow_w = 28.f;          // 拆分/下拉：右侧箭头列宽
    std::vector<std::wstring> items;    // 拆分/下拉的菜单项
    std::function<void(int)> on_pick;   // 选中第几项
    std::shared_ptr<PopupPanel> panel;
    bool opened = false;
    int  dir = 0;                   // 弹层方向（由 show_popup 决定）

    template <class... Args>
    explicit Button(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        text = o.text.empty() ? o.title : o.text;
        icon = o.icon;
        variant = o.variant;
        click_name = o.click;
        change_name = o.change;
        hint_w = o.width;
        hint_h = o.height;
        flex = o.flex;
        enabled = o.enabled;
        int k = o.button_kind < 0 || o.button_kind > 5 ? 0 : o.button_kind;
        kind = (ButtonKind)k;
        check_on = o.toggled;
        if (o.repeat_ms >= 0) repeat_delay = o.repeat_ms;
        items = o.items;
        on_pick = o.on_pick_index;
        if (o.context) context_items = o.items;
        text_cursor_ = false;
    }

    bool interactive() const override { return enabled; }
    bool checked_state() const override {
        return kind == ButtonKind::Toggle ? check_on : false;
    }
    bool hand_cursor() const override { return enabled && (kind == ButtonKind::Hyperlink || interactive()); }

    // 便于程序直接改状态（不派发事件）
    void set_toggled(bool on) {
        check_on = on;
        if (host) host->request_repaint();
    }
    bool toggled() const { return check_on; }

    Size measure(Painter& p, Size avail) override {
        (void)avail;
        float textw = text.empty() ? 0.f : p.measure_text(text, TextScale::Body).w;
        float iconw = icon.empty() ? 0.f : 16.f;
        float gap = (!text.empty() && !icon.empty()) ? 8.f : 0.f;
        float extra = (kind == ButtonKind::Split || kind == ButtonKind::DropDown) ? arrow_w : 0.f;
        float w = or_default(hint_w, textw + iconw + gap + 28.f + extra);
        float h = or_default(hint_h, 32.f);
        return Size(w, h);
    }

    void paint(Painter& p) override {
        tick_anim();
        if (kind == ButtonKind::Repeat) tick_repeat();
        double t = now_ms();
        float hv = hover_t.at(t);
        float pv = press_t.at(t);
        const Palette& pal = p.palette();

        if (kind == ButtonKind::Hyperlink) { paint_link(p, pal, hv); return; }

        Color fill, stroke_col, text_col;
        bool has_stroke = false;
        bool accent_text = false;

        if (!enabled) {
            fill = pal.control.alpha(pal.dark ? 0.18f : 0.45f);
            text_col = pal.text_tertiary;
        } else if (variant == Variant::Primary && kind == ButtonKind::Standard) {
            fill = state_color(pal.accent, pal.accent_hover, pal.accent_press, hv, pv);
            text_col = pal.on_accent;
        } else if (kind == ButtonKind::Toggle && check_on) {
            fill = pal.accent.alpha(0.16f + 0.06f * pv);
            stroke_col = pal.accent.alpha(0.85f);
            text_col = pal.accent;
            has_stroke = true;
            accent_text = true;
        } else if (variant == Variant::Primary) {
            fill = state_color(pal.accent, pal.accent_hover, pal.accent_press, hv, pv);
            text_col = pal.on_accent;
        } else if (variant == Variant::Standard || kind == ButtonKind::Split ||
                   kind == ButtonKind::DropDown) {
            fill = state_color(pal.control, pal.control_hover, pal.control_press, hv, pv);
            stroke_col = pal.control_stroke;
            text_col = pal.text;
            has_stroke = true;
        } else {
            fill = Color(pal.text.r, pal.text.g, pal.text.b, 0.05f * hv + 0.09f * pv);
            text_col = pal.text;
        }
        (void)accent_text;

        if (fill.a > 0.001f) {
            if (cast_shadow) p.shadow(bounds, Radius::control, 7.f, pal.shadow, 1.f);
            p.fill_round(bounds, Radius::control, fill);
        }
        if (has_stroke) {
            p.stroke_round(bounds.inset(0.5f), Radius::control, stroke_col, 1.f);
        }

        bool with_arrow = (kind == ButtonKind::Split || kind == ButtonKind::DropDown);
        float area_w = with_arrow ? bounds.w - arrow_w : bounds.w;

        // 图标 + 文字整体居中
        float textw = text.empty() ? 0.f : p.measure_text(text, TextScale::Body).w;
        float iconw = icon.empty() ? 0.f : 16.f;
        float gap = (!text.empty() && !icon.empty()) ? 8.f : 0.f;
        float total = textw + iconw + gap;
        float x = bounds.x + (area_w - total) * 0.5f;
        float cy = bounds.y + bounds.h * 0.5f;

        if (!icon.empty()) {
            p.glyph(icon, Rect(x, cy - 8.f, 16.f, 16.f), 15.f, text_col);
            x += iconw + gap;
        }
        if (!text.empty()) {
            p.text(text, Rect(x, bounds.y, textw + 1.f, bounds.h), TextScale::Body, text_col,
                   HAlign::Start, VAlign::Middle, false);
        }

        if (with_arrow) {
            Rect aw(bounds.right() - arrow_w, bounds.y, arrow_w, bounds.h);
            p.fill_rect(Rect(aw.x, bounds.y + 6.f, 1.f, bounds.h - 12.f),
                        pal.control_stroke.alpha(0.5f));
            float hv_arrow = hover_arrow_ ? 1.f : 0.f;
            if (hv_arrow > 0.f) p.fill_round(aw.inset(1.f), Radius::control,
                                             pal.dark ? Color(1,1,1,0.06f) : Color(0,0,0,0.04f));
            p.glyph(L"\uE70D", Rect(aw.x + (aw.w - 12.f) * 0.5f, cy - 6.f, 12.f, 12.f),
                    10.f, enabled ? pal.text_secondary : pal.text_tertiary);
        }
    }

    // 超链接：没有底和边框，只有彩色文字；悬浮时出现一条下划线
    void paint_link(Painter& p, const Palette& pal, float hv) {
        Color c = enabled ? (hv > 0.f ? pal.accent_hover : pal.accent) : pal.text_tertiary;
        p.text(text, bounds, TextScale::Body, c, HAlign::Start, VAlign::Middle, false);
        if (hv > 0.02f && enabled) {
            float tw = p.measure_text(text, TextScale::Body).w;
            if (!text.empty()) {
                p.fill_rect(Rect(bounds.x, bounds.y + bounds.h * 0.5f + 8.f, tw * hv, 1.f),
                            c.alpha(0.85f));
            }
        }
    }

    void on_click_at(Point pt) override {
        if (!enabled) return;
        if (kind == ButtonKind::Split) {
            opened = false;
            if (pt.x > bounds.right() - arrow_w) { open_menu_(); return; }
            fire_click();
            return;
        }
        if (kind == ButtonKind::DropDown) { open_menu_(); return; }
        if (kind == ButtonKind::Toggle) {
            check_on = !check_on;
            fire_click();
            if (host) host->request_repaint();
            return;
        }
        if (kind == ButtonKind::Repeat) return;   // 按下就在 on_press 里开火，UP 不再重复
        Widget::on_click_at(pt);
    }
    void on_press(bool on) override {
        if (kind == ButtonKind::Repeat) {
            // 重复按钮：按下即触发一次，按住超过 repeat_delay 后每 repeat_every 再来一次
            if (on && enabled) {
                fire_click();
                repeat_t0 = repeat_last = now_ms();
                repeat_on = true;
                if (host) host->mark_animating();
            } else {
                repeat_on = false;
            }
            Widget::on_press(on);
            return;
        }
        if (!on) repeat_on = false;
        Widget::on_press(on);
    }
    void on_move(Point pt) override {
        if (kind == ButtonKind::Split || kind == ButtonKind::DropDown) {
            bool on = pt.x > bounds.right() - arrow_w;
            if (on != hover_arrow_) {
                hover_arrow_ = on;
                if (host) host->request_repaint();
            }
        }
    }
    void on_hover(bool on) override {
        Widget::on_hover(on);
        if (!on) { hover_arrow_ = false; repeat_on = false; }
    }

    void open_menu_();

protected:
    void fire_click() {
        if (host && !click_name.empty()) host->fire(click_name, this, checked_state());
    }
    void tick_repeat() {
        if (!repeat_on || !host) return;
        double t = now_ms();
        if (t - repeat_t0 < (double)repeat_delay) { host->mark_animating(); return; }
        if (t - repeat_last >= (double)repeat_every) {
            repeat_last = t;
            fire_click();
        }
        host->mark_animating();
    }

    bool   hover_arrow_ = false;
    bool   repeat_on = false;
    bool   text_cursor_ = false;
    double repeat_t0 = 0.0;
    double repeat_last = 0.0;
};

// --------------------------------------------------------------- TextBox ----
// 单行输入框。聚焦后吃键盘：字符、退格、Delete、左右/Home/End、Ctrl+A/C/V/X；
// 中文走 WM_CHAR（IME 提交出来的字就是普通 WM_CHAR），密码模式每个字符画成圆点。
class TextBox : public Widget {
public:
    std::wstring text;
    std::wstring placeholder;
    bool  password = false;
    bool  readonly = false;
    bool  acrylic = false;        // 磨砂底（kw::acrylic() 打开），默认实色
    int   max_length = -1;
    TextScale scale = TextScale::Body;
    float pad = 10.f;

    bool   focused = false;
    size_t caret = 0;
    size_t anchor = 0;
    float  scroll_x = 0.f;
    double blink_t0 = 0.0;
    double focus_t0 = 0.0;          // 下划线展开/收回的起点（0.5s）
    float  focus_e = 0.f;           // 0 = 无线（未聚焦）；1 = 满宽 / 97% 不透明
    bool   focus_anim = false;

    std::function<void(const std::wstring&)> on_change;
    std::function<void(bool)> on_focus_change;   // 聚焦 / 失焦（默认空；Window::attach_numpad 会挂在这）
    bool  numpad_on_focus = false;               // 声明：这个框要配数字键盘（库本身不据此做任何事）
    bool  num_integer = false;                   // 整数框：字符 '.' 直接吃掉，配的数字键盘小数点键灰显 + 不可点

    template <class... Args>
    explicit TextBox(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        text = o.text.empty() ? o.title : o.text;
        placeholder = o.placeholder;
        password = o.password;
        readonly = o.readonly;
        acrylic = o.acrylic > 0;
        max_length = o.max_length;
        click_name = o.click;
        change_name = o.change.empty() ? o.click : o.change;
        if (o.context) context_items = o.items;
        hint_w = o.width;
        hint_h = o.height;
        flex = o.flex;
        enabled = o.enabled;
        caret = anchor = text.size();
        blink_t0 = now_ms();
    }

    bool interactive() const override { return enabled; }
    bool text_cursor() const override { return true; }
    bool wants_key() const override { return true; }

    Size measure(Painter& p, Size avail) override {
        (void)p; (void)avail;
        return Size(or_default(hint_w, 0.f), or_default(hint_h, 34.f));
    }

    // 出图用：把下划线展开到指定毫秒（0..500），-1 = 正常按时间走
    void set_focus_time(double ms) { focus_time_ = ms; }

    // 出图用：直接把下划线置到终态（不播那 0.5s）
    void skip_focus_animation() { focus_e = focused ? 1.f : 0.f; focus_anim = false; }

    void set_text(const std::wstring& t) {
        text = t;
        if (max_length > 0 && (int)text.size() > max_length) text.resize((size_t)max_length);
        caret = anchor = text.size();
        scroll_x = 0.f;
        blink_t0 = now_ms();
        if (host) host->request_repaint();
    }

    // 文本可绘制区。默认等于整个框；NumberBox 这类"框里还塞了步进按钮"的控件把它缩窄，
    // 这样光标/选区/滚动都自动让开按钮，不用复制一遍绘制代码。
    virtual Rect content_rect() const { return bounds; }

    // 点击：先要焦点，再把光标落到点到的那个字符缝上
    void on_click_at(Point pt) override {
        if (host) host->want_focus(this);
        pending_x_ = pt.x;
        has_pending_ = true;
        reset_anchor_ = true;
    }
    void on_drag(Point pt) override {
        if (!pressed) return;
        pending_x_ = pt.x;
        has_pending_ = true;
        reset_anchor_ = false;
    }
    void on_press(bool on) override {
        Widget::on_press(on);
        if (on && host) host->want_focus(this);
    }
    void on_focus(bool on) override {
        focused = on;
        blink_t0 = now_ms();
        if (!on) anchor = caret;
        // 下划线：聚焦时从正中往外长到满宽，失焦再收回去，都是 0.2s
        focus_t0 = now_ms();
        focus_from_ = focus_e;
        focus_target_ = on ? 1.f : 0.f;
        focus_anim = (focus_from_ != focus_target_);
        if (host) host->request_repaint();
        if (on_focus_change) on_focus_change(on);
    }

    bool on_key(int vk, bool ctrl, bool shift) override {
        if (!enabled) return false;
        if (ctrl) {
            switch (vk) {
                case 'A':
                    anchor = 0; caret = text.size(); blink_t0 = now_ms();
                    if (host) host->request_repaint();
                    return true;
                case 'C': if (sel_len()) clip_set(sel_str()); return true;
                case 'X': if (!readonly && sel_len()) { clip_set(sel_str()); del_sel(); } return true;
                case 'V': if (!readonly) paste(clip_get()); return true;
                default: return false;
            }
        }
        switch (vk) {
            case VK_LEFT:   move_caret(caret > 0 ? caret - 1 : 0, shift); return true;
            case VK_RIGHT:  move_caret(caret < text.size() ? caret + 1 : caret, shift); return true;
            case VK_HOME:   move_caret(0, shift); return true;
            case VK_END:    move_caret(text.size(), shift); return true;
            case VK_BACK:   if (!readonly) backspace(); return true;
            case VK_DELETE: if (!readonly) del_forward(); return true;
            case VK_RETURN:
                if (host && !click_name.empty()) host->fire(click_name, this, false);
                return true;
            default: return false;
        }
    }

    void on_char(wchar_t ch) override {
        if (!enabled || readonly) return;
        if (ch < 0x20 || ch == 0x7F) return;                  // 回车/退格走 on_key
        if (num_integer && ch == L'.') return;                // 整数框：实体键盘敲的小数点也挡掉
        if (max_length > 0 && (int)text.size() >= max_length) return;
        del_sel_quiet();
        text.insert(caret, 1, ch);
        caret++; anchor = caret;
        blink_t0 = now_ms();
        changed();
    }

    void paint(Painter& p) override {
        tick_anim();
        const Palette& pal = p.palette();

        if (has_pending_) {
            has_pending_ = false;
            Rect cr = content_rect();
            size_t idx = index_at(p, pending_x_ - (cr.x + pad - scroll_x));
            caret = idx;
            if (reset_anchor_) anchor = idx;
            blink_t0 = now_ms();
        }

        Color bg = pal.field;
        if (!enabled) bg.a *= 0.5f;
        if (acrylic && enabled) {
            p.fill_glass(bounds, Radius::control);      // 亚克力模式：不做实色，走磨砂面
        } else {
            p.fill_round(bounds, Radius::control, bg);
        }
        p.stroke_round(bounds.inset(0.5f), Radius::control,
                       enabled ? pal.control_stroke : pal.control_stroke.alpha(0.4f), 1.f);

        // 底部那条线：Win11 的输入框就靠它表达"这里能打字"。
        // 未聚焦：没有线（长度 0）；聚焦：满宽、只剩 3% 透明（几乎实色）蓝线；
        // 0.2s 从中间长出来 / 缩回中间，颜色同时灰→蓝。
        if (focus_anim) {
            double dt = focus_time_ >= 0.0 ? focus_time_ : (now_ms() - focus_t0);
            float u = (float)std::min(1.0, dt / 200.0);
            float k = u * u * (3.f - 2.f * u);            // ease-in-out
            focus_e = focus_from_ + (focus_target_ - focus_from_) * k;
            if (u >= 1.f) { focus_e = focus_target_; focus_anim = false; }
            else if (focus_time_ < 0.0 && host) host->mark_animating();
        }
        float le = enabled ? focus_e : 0.f;
        if (le > 0.001f) {
            Color line = mix(pal.field_line, pal.accent, le);
            line.a *= 0.97f;                              // 只剩 3% 透明
            float line_h = 1.f + le;
            float line_len = (bounds.w - 8.f) * le;       // 0 → 满宽（左右各留 4px）
            p.fill_round(Rect(bounds.x + (bounds.w - line_len) * 0.5f,
                              bounds.y + bounds.h - line_h, line_len, line_h),
                          line_h * 0.5f, line);
        }

        ensure_caret_visible(p);
        Rect cr = content_rect();
        p.push_clip(Rect(cr.x + 1.f, cr.y + 1.f, cr.w - 2.f, cr.h - 2.f));
        float ox = cr.x + pad - scroll_x;

        if (focused && sel_len()) {
            float x0 = ox + x_at(p, sel_a());
            float x1 = ox + x_at(p, sel_b());
            p.fill_rect(Rect(x0, bounds.y + 5.f, std::max(1.f, x1 - x0), bounds.h - 10.f),
                        pal.accent.alpha(0.22f));
        }

        bool show_ph = text.empty() && !placeholder.empty();
        std::wstring shown = show_ph ? placeholder : display_text();
        Color tc = show_ph ? pal.text_tertiary : (enabled ? pal.text : pal.text_tertiary);
        p.text(shown, Rect(ox, bounds.y, cr.w, bounds.h), scale, tc,
               HAlign::Start, VAlign::Middle, false);

        if (focused && blink_on()) {
            float cx = ox + x_at(p, caret);
            p.fill_rect(Rect(cx, bounds.y + 7.f, 1.5f, bounds.h - 14.f), pal.accent);
        }
        p.pop_clip();

        // 光标闪烁：0.5s 亮 / 0.5s 灭。只在"快到切换点"的那 40ms 里补几帧 ——
        // 必须自己 request_repaint，光 mark_animating 主循环不会推帧，光标就定在那不动。
        if (focused && host) {
            double ph = std::fmod(now_ms() - blink_t0, 1000.0);
            double left = (ph < 500.0 ? 500.0 : 1000.0) - ph;
            if (left < 40.0) { host->mark_animating(); host->request_repaint(); }
        }
    }

private:
    float pending_x_ = 0.f;
    bool  has_pending_ = false;
    bool  reset_anchor_ = true;
    float focus_from_ = 0.f;
    float focus_target_ = 0.f;
    double focus_time_ = -1.0;      // >= 0：展开进度钉死（出图看中间帧）

    std::wstring display_text() const {
        return password ? std::wstring(text.size(), (wchar_t)0x2022) : text;
    }
    size_t sel_a() const { return std::min(anchor, caret); }
    size_t sel_b() const { return std::max(anchor, caret); }
    size_t sel_len() const { return sel_b() - sel_a(); }
    std::wstring sel_str() const {
        return sel_len() ? text.substr(sel_a(), sel_len()) : std::wstring();
    }
    bool blink_on() const {
        if (!focused) return false;
        double ph = std::fmod(now_ms() - blink_t0, 1000.0);
        return ph < 500.0;
    }
    void move_caret(size_t pos, bool extend) {
        caret = std::min(pos, text.size());
        if (!extend) anchor = caret;
        blink_t0 = now_ms();
        if (host) host->request_repaint();
    }
    void del_sel_quiet() {
        if (sel_len()) {
            text.erase(sel_a(), sel_len());
            caret = anchor = sel_a();
        }
    }
    void del_sel() { del_sel_quiet(); changed(); }
    void backspace() {
        if (sel_len()) { del_sel(); return; }
        if (caret > 0) { text.erase(caret - 1, 1); caret--; anchor = caret; changed(); }
    }
    void del_forward() {
        if (sel_len()) { del_sel(); return; }
        if (caret < text.size()) { text.erase(caret, 1); changed(); }
    }
    void paste(const std::wstring& s) {
        if (s.empty()) return;
        std::wstring t;
        for (wchar_t c : s) if (c >= 0x20 && c != 0x7F) t.push_back(c);
        if (t.empty()) return;
        del_sel_quiet();
        if (max_length > 0) {
            int room = max_length - (int)text.size();
            if (room <= 0) return;
            if ((int)t.size() > room) t.resize((size_t)room);
        }
        text.insert(caret, t);
        caret += t.size();
        anchor = caret;
        blink_t0 = now_ms();
        changed();
    }
    void changed() {
        if (!host) return;
        host->request_repaint();
        if (on_change) on_change(text);
        if (!change_name.empty()) host->fire(change_name, this, checked_state());
    }
    float x_at(Painter& p, size_t idx) {
        if (idx == 0) return 0.f;
        std::wstring s = display_text();
        if (idx > s.size()) idx = s.size();
        return p.measure_text(s.substr(0, idx), scale).w;
    }
    size_t index_at(Painter& p, float x) {
        std::wstring s = display_text();
        if (s.empty()) return 0;
        float best = 1e9f;
        size_t idx = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            float d = std::fabs(x_at(p, i) - x);
            if (d < best) { best = d; idx = i; }
        }
        return idx;
    }
    void ensure_caret_visible(Painter& p) {
        float view = content_rect().w - pad * 2.f;
        if (view <= 1.f) return;
        float cx = x_at(p, caret);
        if (cx - scroll_x > view - 2.f) scroll_x = cx - view + 2.f;
        if (cx - scroll_x < 0.f) scroll_x = cx;
        if (scroll_x < 0.f) scroll_x = 0.f;
    }

    static std::wstring clip_get() {
        std::wstring r;
        if (!OpenClipboard(nullptr)) return r;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* s = (const wchar_t*)GlobalLock(h)) {
                r.assign(s);
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return r;
    }
    static void clip_set(const std::wstring& s) {
        if (s.empty() || !OpenClipboard(nullptr)) return;
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
        if (h) {
            if (wchar_t* d = (wchar_t*)GlobalLock(h)) {
                for (size_t i = 0; i <= s.size(); ++i) d[i] = s[i];
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
        }
        CloseClipboard();
    }
};

// ------------------------------------------------------------ NumberBox ----
// 数字框。和输入框一样能打字（数字 / 小数点 / 负号），另外带三种步进样式：
//   Style::Spin      —— 框内右端上下两个小箭头（Win11 默认，参考图就是这个）
//   Style::Steppers  —— 框左右两端各一个 − / + 方按钮
//   Style::Plain     —— 不画按钮，只在键盘 ↑↓ / PageUpDown / API 上步进
// 值、上下限、步长、小数位、单位后缀、字形全部由调用方传入；不给就按最朴素的默认走。
// 按住箭头会连发（默认 400ms 后每 60ms 一步）；滚轮不接（Scene 目前没有分发 wheel）。
class NumberBox : public TextBox {
public:
    enum class Style { Spin, Steppers, Plain };

    double value = 0.0;
    double min_value = -1e9;
    double max_value = 1e9;
    double step = 1.0;
    int    decimals = 0;                 // 0 = 只显示整数
    std::wstring suffix;                 // 单位（"px" / "%" / "ms"）；空 = 不画
    float  suffix_lead = 14.f;           // 单位左边到文本区的距离
    float  suffix_gap  = 20.f;           // 单位右边到分隔线（或框右缘）的距离
    Style  style = Style::Spin;
    float  arrow_w = 20.f;               // Spin：右侧箭头列宽
    float  btn_w = 32.f;                 // Steppers：两侧按钮宽
    long   repeat_delay = 400;           // 按住多久开始连发
    long   repeat_every = 60;            // 连发间隔

    std::wstring glyph_up   = L"\uE70E";  // ChevronUp
    std::wstring glyph_down = L"\uE70D";  // ChevronDown
    std::wstring glyph_plus = L"\uE710";  // Add
    std::wstring glyph_minus = L"\uE738"; // Remove

    std::function<void(double)> on_value;

    template <class... Args>
    explicit NumberBox(Args&&... args) : TextBox(std::forward<Args>(args)...) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        min_value = o.num_min;
        max_value = o.num_max;
        step = o.num_step;
        decimals = o.num_decimals;
        suffix = o.suffix;
        style = (Style)(o.num_style < 0 || o.num_style > 2 ? 0 : o.num_style);
        if (o.repeat_delay_ms >= 0) repeat_delay = o.repeat_delay_ms;
        on_value = o.on_value;
        double start = o.num_value;
        if (!o.text.empty()) {          // 给了文字就按文字算，否则用 kw::num 的值
            double parsed = 0.0;
            if (parse_value(o.text, parsed)) start = parsed;
        }
        value = start;
        set_value(value, false);
    }

    Size measure(Painter& p, Size avail) override {
        (void)avail;
        return Size(or_default(hint_w, 120.f), or_default(hint_h, 34.f));
    }

    // 文本区让开步进按钮 / 单位后缀，按钮就自然嵌在框里（不用复制一遍绘制）
    Rect content_rect() const override {
        Rect r = bounds;
        if (style == Style::Spin) r.w -= arrow_w;
        else if (style == Style::Steppers) { r.x += btn_w; r.w -= btn_w * 2.f; }
        if (!suffix.empty()) r.w -= suffix_w_ + suffix_lead + suffix_gap;
        if (r.w < 8.f) r.w = 8.f;
        return r;
    }

    void set_value(double v, bool notify = true) {
        if (v < min_value) v = min_value;
        if (v > max_value) v = max_value;
        // 抹掉 "0.30000000000000004" 这类浮点尾巴
        double eps = std::pow(10.0, -(double)(decimals + 3));
        v = std::round(v / eps) * eps;
        value = v;
        text = format_value(value);
        caret = anchor = text.size();
        scroll_x = 0.f;
        if (notify) fire_value();
    }

    double value_now() const { return value; }

    void paint(Painter& p) override {
        suffix_w_ = suffix.empty() ? 0.f : p.measure_text(suffix, TextScale::Caption).w;
        // 长按连发：只要还按着就得持续要帧，否则 400ms 之后没有重绘、连发起不来
        if (repeat_dir_ != 0) {
            // 兜底：切窗口 / 拖出控件再拖回来没收到 UP 的情况，别让连发一直跑
            if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) repeat_dir_ = 0;
        }
        if (repeat_dir_ != 0) {
            double t = now_ms();
            if (t - repeat_t0 >= (double)repeat_delay) {
                if (t - repeat_last >= (double)repeat_every) {
                    repeat_last = t;
                    nudge(repeat_dir_);
                }
            }
            if (host) host->mark_animating();
        }
        TextBox::paint(p);
        draw_unit(p);
        draw_steppers(p);
    }

protected:
    void draw_unit(Painter& p) {
        if (suffix.empty()) return;
        const Palette& pal = p.palette();
        Rect cr = content_rect();
        p.text(suffix, Rect(cr.right() + suffix_lead, bounds.y, suffix_w_, bounds.h),
               TextScale::Caption, pal.text_tertiary, HAlign::Start, VAlign::Middle, false);
    }

    void draw_steppers(Painter& p) {
        if (style == Style::Plain) return;
        const Palette& pal = p.palette();
        Color hot = pal.dark ? Color(1.f, 1.f, 1.f, 0.06f) : Color(0.f, 0.f, 0.f, 0.04f);
        Color line = pal.control_stroke.alpha(0.45f);

        if (style == Style::Spin) {
            Rect col(bounds.right() - arrow_w, bounds.y, arrow_w, bounds.h);
            p.fill_rect(Rect(col.x, bounds.y + 6.f, 1.f, bounds.h - 12.f), line);
            float half = bounds.h * 0.5f;
            Rect up(col.x, bounds.y + 1.f, col.w, half - 1.f);
            Rect dn(col.x, bounds.y + half, col.w, bounds.h - half - 1.f);
            if (hover_dir_ == 1) p.fill_round(up.inset(1.f), Radius::control, hot);
            if (hover_dir_ == -1) p.fill_round(dn.inset(1.f), Radius::control, hot);
            Color gc = enabled ? pal.text_secondary : pal.text_tertiary;
            p.glyph(glyph_up, Rect(up.x + (up.w - 14.f) * 0.5f, up.y + up.h * 0.5f - 6.f, 14.f, 12.f), 10.f, gc);
            p.glyph(glyph_down, Rect(dn.x + (dn.w - 14.f) * 0.5f, dn.y + dn.h * 0.5f - 6.f, 14.f, 12.f), 10.f, gc);
            return;
        }
        // Steppers：左右两个方按钮，各带一条分隔线
        Rect lp(bounds.x, bounds.y, btn_w, bounds.h);
        Rect rp(bounds.right() - btn_w, bounds.y, btn_w, bounds.h);
        if (hover_dir_ == 1) p.fill_round(rp.inset(1.f), Radius::control, hot);
        if (hover_dir_ == -1) p.fill_round(lp.inset(1.f), Radius::control, hot);
        p.fill_rect(Rect(lp.right() - 1.f, bounds.y + 6.f, 1.f, bounds.h - 12.f), line);
        p.fill_rect(Rect(rp.x, bounds.y + 6.f, 1.f, bounds.h - 12.f), line);
        Color gc = enabled ? pal.text_secondary : pal.text_tertiary;
        p.glyph(glyph_minus, Rect(lp.x + lp.w * 0.5f - 7.f, lp.y + lp.h * 0.5f - 7.f, 14.f, 14.f), 11.f, gc);
        p.glyph(glyph_plus,  Rect(rp.x + rp.w * 0.5f - 7.f, rp.y + rp.h * 0.5f - 7.f, 14.f, 14.f), 11.f, gc);
    }

    // 点在哪个步进按钮上：+1 加 / -1 减 / 0 文本区
    int dir_at(Point pt) const {
        if (!enabled) return 0;
        if (style == Style::Spin) {
            if (pt.x < bounds.right() - arrow_w) return 0;
            return pt.y < bounds.y + bounds.h * 0.5f ? 1 : -1;
        }
        if (style == Style::Steppers) {
            if (pt.x < bounds.x + btn_w) return -1;
            if (pt.x > bounds.right() - btn_w) return 1;
        }
        return 0;
    }

public:
    // 自测用：现在是否还在连发（单击之后必须是 false）
    bool repeating() const { return repeat_dir_ != 0; }
    void on_click_at(Point pt) override {
        // 步进按钮已经在 on_press(true) 里加过一次并开了连发，这里不再重复加
        if (dir_at(pt) != 0) return;
        clear_repeat();
        TextBox::on_click_at(pt);
    }
    void on_press(bool on) override {
        if (on) {
            // 按下就加一次，并开始计时；hover_dir_ 在 mouse_down 之前的 mouse_move 里已经算好
            int d = hover_dir_;
            if (d != 0 && enabled) {
                nudge(d);
                repeat_dir_ = d;
                repeat_t0 = repeat_last = now_ms();
            }
        } else {
            clear_repeat();       // 抬起（含拖出控件 / 失焦）立刻停止连发
        }
        TextBox::on_press(on);
    }
    void on_move(Point pt) override { hover_dir_ = dir_at(pt); }
    void on_hover(bool on) override {
        TextBox::on_hover(on);
        if (!on) hover_dir_ = 0;
    }
    void on_focus(bool on) override {
        TextBox::on_focus(on);
        if (!on) { clear_repeat(); commit(); }   // 失焦：连发停掉，越界输入夹回范围
    }
    bool on_key(int vk, bool ctrl, bool shift) override {
        if (!enabled) return false;
        switch (vk) {
            case VK_UP:    nudge(1); return true;
            case VK_DOWN:  nudge(-1); return true;
            case VK_PRIOR: nudge(10); return true;
            case VK_NEXT:  nudge(-10); return true;
            default: break;
        }
        if (vk == VK_RETURN) { commit(); }
        return TextBox::on_key(vk, ctrl, shift);
    }
    void on_char(wchar_t ch) override {
        if (!enabled || readonly) return;
        bool digit = (ch >= L'0' && ch <= L'9');
        bool dot = (ch == L'.' && decimals > 0);
        bool neg = (ch == L'-' && text.empty() && caret == 0);
        if (!digit && !dot && !neg) return;
        TextBox::on_char(ch);
        sync_from_text();
    }

protected:
    void nudge(double dir) {
        if (!enabled) return;
        set_value(value + dir * step);
    }
    void clear_repeat() { repeat_dir_ = 0; }
    void commit() {
        double parsed = value;
        if (!parse_value(text, parsed)) parsed = value;
        set_value(parsed);
    }
    void sync_from_text() {
        double parsed = 0.0;
        if (!parse_value(text, parsed)) return;   // 打到一半（"-" / "" / "1."）先不动
        if (parsed == value) return;
        value = parsed;                            // 暂时不夹取，免得把 "-5" 夹成下限打断输入
        fire_value();
    }
    void fire_value() {
        if (host) host->request_repaint();
        if (on_value) on_value(value);
        if (!change_name.empty() && host) host->fire(change_name, this, false);
    }

    std::wstring format_value(double v) const {
        wchar_t buf[64];
        if (decimals > 0) std::swprintf(buf, 64, L"%.*f", decimals, v);
        else std::swprintf(buf, 64, L"%.0f", v);
        return std::wstring(buf);
    }
    static bool parse_value(const std::wstring& s, double& out) {
        if (s.empty()) return false;
        wchar_t* end = nullptr;
        double v = std::wcstod(s.c_str(), &end);
        if (end == s.c_str()) return false;
        out = v;
        return true;
    }

    mutable float suffix_w_ = 0.f;
    int    hover_dir_ = 0;        // 1 = 加粗按钮 hover，-1 = 减
    int    repeat_dir_ = 0;
    double repeat_t0 = 0.0;
    double repeat_last = 0.0;
};

// ---------------------------------------------------------------- Dialog ----
// 模态弹窗：整屏遮罩 + 居中面板，打开淡入并上浮 12px。
// Esc = 取消，Enter = 确定；点遮罩默认也取消（kw::close_on_scrim(false) 可关）。
// 面板内容可以整块换掉：dialog->body = ui::textbox(...)。
class Dialog : public Widget {
public:
    std::wstring title;
    std::wstring message;
    std::wstring ok_text;
    std::wstring cancel_text;
    std::shared_ptr<Widget> body;
    std::function<void(bool)> on_result;
    bool  close_on_scrim = true;
    bool  draggable = false;      // 按住面板（标题/消息/空白）拖动。**默认关**：想拖要显式开
    bool  acrylic = false;        // 面板走磨砂面（kw::acrylic() 打开），默认实色面板
    float min_width = 420.f;

    Rect panel;
    Rect panel0;                  // 居中时的基准位置（拖动偏移相对它算）
    Rect ok_r, cancel_r, body_r;

    template <class... Args>
    explicit Dialog(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        title = o.title;
        message = o.text;
        ok_text = o.ok_text;
        cancel_text = o.cancel_text;
        close_on_scrim = o.close_on_scrim;
        draggable = o.draggable;
        acrylic = o.acrylic > 0;
        min_width = or_default(o.min_width, 420.f);
    }

    bool interactive() const override { return true; }
    // 关闭动画还没走完就不能销毁（Scene::sweep_modal 靠这个判断）
    bool finished() const {
        if (!done_) return false;
        if (!close_anim) return true;
        return (now_ms() - close_t0_) >= (double)close_ms;
    }
    // 离屏出图用：跳过 400ms 的打开动画，第一帧就是终态
    void skip_open_animation() {
        started_ = true;
        t0_ = now_ms() - 1000.0;
        e_ = 1.f;
    }
    // 自测 / 立即关闭用：跳过 400ms 的关闭动画（下一帧就直接摘掉这一层）
    void skip_close_animation() { close_anim = false; }
    // 出图 / 停在中间看一帧：把"已经打开多久"钉死（毫秒），-1 = 正常按时间走
    void set_open_time(double ms) { started_ = true; open_time_ = ms; }
    // 出图用：把关闭动画钉在第 N 毫秒（0..close_ms），顺带把面板当成"已打开"
    void set_close_time(double ms) {
        started_ = true;
        closing_ = true;
        done_ = true;
        close_time_ = ms;
    }

    // 全屏都归这层：遮罩拦住下面的控件，也让"点遮罩关闭"能生效
    Widget* hit_test(Point pt) override { (void)pt; return this; }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        rebuild(p);
    }

    void on_hover(bool on) override { (void)on; }
    void on_move(Point pt) override {
        last_pt_ = pt;
        bool a = !ok_text.empty() && ok_r.w > 0.f && ok_r.contains(pt);
        bool b = !cancel_text.empty() && cancel_r.w > 0.f && cancel_r.contains(pt);
        if (a != hov_ok || b != hov_cancel) {
            hov_ok = a;
            hov_cancel = b;
            if (host) host->request_repaint();
        }
    }
    // 按下/拖动/点击都要往下转给面板里的内容（不然弹窗里的输入框点不进去）
    void on_press(bool on) override {
        if (!on) {
            dragging_ = false;
            if (body && body->pressed) body->on_press(false);
            return;
        }
        // 可拖区域 = 面板里除"内容区 / 按钮"以外的部分（标题、消息、留白）
        bool on_body = body && body_r.w > 0.f && body_r.contains(last_pt_);
        bool on_btn  = (!ok_text.empty() && ok_r.w > 0.f && ok_r.contains(last_pt_)) ||
                       (!cancel_text.empty() && cancel_r.w > 0.f && cancel_r.contains(last_pt_));
        if (draggable && panel.contains(last_pt_) && !on_body && !on_btn) {
            down_pt_ = last_pt_;
            drag_rel_ = Point(last_pt_.x - panel.x, last_pt_.y - panel.y);
            dragging_ = true;
            drag_moved_ = false;
            return;
        }
        if (on_body) body->on_press(true);
    }
    void on_drag(Point pt) override {
        if (dragging_) {
            const float m = 12.f;   // 面板至少留 12px 在客户区里，别整个拖出去找不回来
            float nx = std::min(std::max(pt.x - drag_rel_.x, -panel.w + m), bounds.w - m);
            float ny = std::min(std::max(pt.y - drag_rel_.y, -panel.h + m), bounds.h - m);
            float dx = nx - panel.x, dy = ny - panel.y;
            if (std::fabs(pt.x - down_pt_.x) > 3.f || std::fabs(pt.y - down_pt_.y) > 3.f)
                drag_moved_ = true;
            // 子矩形跟着一起挪，否则拖动时输入框和按钮会留在原地
            panel.x = nx; panel.y = ny;
            body_r.x += dx; body_r.y += dy;
            if (ok_r.w > 0.f) { ok_r.x += dx; ok_r.y += dy; }
            if (cancel_r.w > 0.f) { cancel_r.x += dx; cancel_r.y += dy; }
            title_top_ += dy; msg_top_ += dy;
            if (host) host->request_repaint();
            return;
        }
        if (body && body->pressed) body->on_drag(pt);
    }
    void on_click_at(Point pt) override {
        if (drag_moved_) { drag_moved_ = false; return; }   // 拖完松手不算点击
        if (!ok_text.empty() && ok_r.w > 0.f && ok_r.contains(pt)) { done(true); return; }
        if (!cancel_text.empty() && cancel_r.w > 0.f && cancel_r.contains(pt)) { done(false); return; }
        if (body && body_r.contains(pt)) { body->on_click_at(pt); return; }
        if (!panel.contains(pt) && close_on_scrim) done(false);
    }
    bool on_key(int vk, bool ctrl, bool shift) override {
        (void)ctrl; (void)shift;
        if (vk == VK_ESCAPE) { done(false); return true; }
        if (vk == VK_RETURN && !ok_text.empty()) { done(true); return true; }
        return false;
    }

    void paint(Painter& p) override {
        double now = now_ms();
        if (!started_) { started_ = true; t0_ = now; }

        float scale_k = 0.f;   // 0 = 85%，1 = 100%
        float panel_a = 1.f;   // 面板整体不透明度
        float mask_k = 0.f;    // 遮罩进度 0..1（× pal.scrim.a）
        if (closing_) {
            // 关闭 200ms：缩回 85%，面板从 100% 淡到 20%，遮罩 40% → 0；
            // 到 20% 那一帧（uc = 1）立刻摘层，不再显示
            double dt = close_time_ >= 0.0 ? close_time_ : (now - close_t0_);
            double uc = close_anim ? std::min(1.0, dt / (double)close_ms) : 1.0;
            float u = (float)uc;
            scale_k = 1.f - u;
            panel_a = 1.f - 0.80f * u;
            mask_k = 1.f - u;
            if (uc < 1.0 && host) host->mark_animating();
            else if (close_time_ < 0.0) {
                // 淡到 20% 就收：这一帧不再画面板（接下来的 sweep_modal 会把模态层摘掉）
                return;
            }
        } else {
            double dt = open_time_ >= 0.0 ? open_time_ : (now - t0_);
            float u = (float)std::min(1.0, dt / (double)open_ms);
            float k = 1.f - std::pow(1.f - u, 3.f);
            scale_k = k;
            panel_a = 1.f;                 // 打开不放淡入：面板直接不透明地"长大"
            mask_k = k;
            if (u < 1.f && open_time_ < 0.0 && host) host->mark_animating();
        }
        e_ = scale_k;

        const Palette& pal = p.palette();

        // 遮罩：0 → 40%（pal.scrim 的 alpha 就是 0.40，这里只乘进度）
        Color scrim = pal.scrim;
        scrim.a *= mask_k;
        p.fill_rect(bounds, scrim);

        // 面板：绕中心 85% → 100%。关闭时整层一起淡到 20%（含 body 里的控件，
        // 所以走 PushLayer 而不是给每个颜色乘 alpha）
        float s = from_scale + (1.f - from_scale) * scale_k;
        Point c(panel.x + panel.w * 0.5f, panel.y + panel.h * 0.5f);
        if (body) body->arrange(p, body_r);      // 布局用未缩放坐标
        p.push_opacity(panel_a, bounds);
        p.push_scale(c.x, c.y, s);

        p.shadow(panel, Radius::flyout, 18.f, pal.shadow, 8.f);
        if (acrylic) {
            p.fill_glass(panel, Radius::flyout);        // 亚克力面板：磨砂面自带亮边
        } else {
            p.fill_round(panel, Radius::flyout, pal.flyout);
            p.stroke_round(panel.inset(0.5f), Radius::flyout, pal.flyout_border, 1.f);
        }

        float pad = 24.f;
        if (!title.empty()) {
            p.text(title, Rect(panel.x + pad, title_top_, panel.w - pad * 2.f, 28.f),
                   TextScale::Subtitle, pal.text, HAlign::Start, VAlign::Middle);
        }
        if (!message.empty()) {
            p.text(message, Rect(panel.x + pad, msg_top_, panel.w - pad * 2.f, msg_h_),
                   TextScale::Body, pal.text_secondary, HAlign::Start, VAlign::Top, true);
        }
        if (body) body->paint(p);
        if (!cancel_text.empty() && cancel_r.w > 0.f) {
            draw_btn(p, cancel_r, cancel_text, false, hov_cancel);
        }
        if (!ok_text.empty() && ok_r.w > 0.f) {
            draw_btn(p, ok_r, ok_text, true, hov_ok);
        }
        p.pop_scale();
        p.pop_opacity();
    }

private:
    double t0_ = 0.0;
    double open_time_ = -1.0;       // >= 0：打开进度钉死（出图看中间帧）
    double close_time_ = -1.0;      // >= 0：关闭进度钉死
    bool   started_ = false;
    float  e_ = 0.f;
    bool   done_ = false;
    bool   closing_ = false;        // 关闭动画进行中
    bool   close_anim = true;       // 关掉就是没有口号动画（自测用）
    double close_t0_ = 0.0;
    float  open_ms = 200.f;         // 打开：85% → 100%，0.2s
    float  close_ms = 200.f;        // 关闭：反过来，面板同时淡到 20%（0.2s）
    float  from_scale = 0.85f;
    bool   hov_ok = false, hov_cancel = false;
    bool   focus_set_ = false;
    Point  last_pt_;
    Point  down_pt_{0, 0};        // 拖动起点（判定"是拖还是点"）
    Point  drag_rel_{0, 0};       // 按下点相对面板左上角
    Point  off_{0, 0};            // 拖动累积偏移（相对居中位置）
    bool   dragging_ = false;
    bool   drag_moved_ = false;
    float  title_top_ = 0.f, msg_top_ = 0.f, msg_h_ = 0.f;

    void done(bool ok) {
        if (done_) return;
        done_ = true;
        if (on_result) on_result(ok);          // 结果是"立刻"给的，动画只是收尾
        closing_ = true;
        close_t0_ = now_ms();
        if (host) host->request_repaint();
    }

    void draw_btn(Painter& p, const Rect& r, const std::wstring& t, bool primary, bool hov) {
        const Palette& pal = p.palette();
        Color fill = primary ? (hov ? pal.accent_hover : pal.accent)
                             : (hov ? pal.control_hover : pal.control);
        p.fill_round(r, Radius::control, fill);
        if (!primary) p.stroke_round(r.inset(0.5f), Radius::control, pal.control_stroke, 1.f);
        p.text(t, r, TextScale::Body, primary ? pal.on_accent : pal.text,
               HAlign::Center, VAlign::Middle, false);
    }

    void rebuild(Painter& p) {
        float pad = 24.f;
        float w = std::max(min_width, 420.f);
        float maxw = bounds.w - 48.f;
        if (w > maxw) w = maxw;
        if (w < 160.f) w = bounds.w;
        float inner = std::max(40.f, w - pad * 2.f);

        float title_h = title.empty() ? 0.f : 30.f;
        float msg_h = 0.f;
        if (!message.empty()) msg_h = p.measure_text(message, TextScale::Body, true, inner).h + 10.f;

        float body_h = 0.f;
        if (body) {
            body->attach(host);
            Size bs = body->measure(p, Size(inner, 0.f));
            body_h = std::max(34.f, bs.h);
            if (title_h > 0.f || msg_h > 0.f) body_h += 12.f;
        }
        float btn_h = (ok_text.empty() && cancel_text.empty()) ? 0.f : 34.f;
        float total = pad + title_h + msg_h + body_h + (btn_h > 0.f ? btn_h + 20.f : 0.f) + pad;
        if (total > bounds.h - 32.f) total = bounds.h - 32.f;
        if (total < 96.f) total = 96.f;

        panel0 = Rect((bounds.w - w) * 0.5f, (bounds.h - total) * 0.5f, w, total);
        panel = panel0;
        if (off_.x != 0.f || off_.y != 0.f) {
            // 拖动后的位置：允许探出去一点，但整块不能跑没（窗口缩小后也要夹回来）
            const float m = 12.f;
            float nx = std::min(std::max(panel0.x + off_.x, -w + m), bounds.w - m);
            float ny = std::min(std::max(panel0.y + off_.y, -total + m), bounds.h - m);
            off_.x = nx - panel0.x;
            off_.y = ny - panel0.y;
            panel.x = nx;
            panel.y = ny;
        }

        float y = panel.y + pad;
        title_top_ = y;
        y += title_h;
        msg_top_ = y;
        msg_h_ = msg_h;
        y += msg_h;
        body_r = Rect(panel.x + pad, y, inner, body_h);

        ok_r = Rect();
        cancel_r = Rect();
        if (btn_h > 0.f) {
            float by = panel.y + panel.h - pad - btn_h;
            float right = panel.x + panel.w - pad;
            if (!ok_text.empty()) {
                float bw = p.measure_text(ok_text, TextScale::Body).w + 32.f;
                ok_r = Rect(right - bw, by, bw, btn_h);
                right -= bw + 8.f;
            }
            if (!cancel_text.empty()) {
                float bw = p.measure_text(cancel_text, TextScale::Body).w + 32.f;
                cancel_r = Rect(right - bw, by, bw, btn_h);
            }
        }

        if (body && !focus_set_ && host) {
            focus_set_ = true;
            if (auto tb = std::dynamic_pointer_cast<TextBox>(body)) host->want_focus(tb.get());
        }
    }
};

// --------------------------------------------------------------- Toggle ----
class Toggle : public Widget {
public:
    std::wstring label;
    bool checked = false;

    template <class... Args>
    explicit Toggle(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        label = o.text.empty() ? o.title : o.text;
        checked = o.checked;
        click_name = o.click;
        change_name = o.change.empty() ? o.click : o.change;
        hint_w = o.width;
        hint_h = o.height;
        enabled = o.enabled;
        knob_.jump(checked ? 1.f : 0.f);
    }

    bool checked_state() const override { return checked; }
    bool interactive() const override { return enabled && (!change_name.empty() || !click_name.empty()); }

    void set_checked(bool v, bool animate = true) {
        if (checked == v) return;
        checked = v;
        knob_.go(v ? 1.f : 0.f, now_ms(), Motion::normal, ease_out_back);
        if (host) host->request_repaint();
    }

    // 面板刚造好时回调一次：挂子菜单、改行高/勾选样式等
    std::function<void(PopupPanel&)> on_panel;

    void on_click() override {
        set_checked(!checked, true);
        if (host && !change_name.empty()) host->fire(change_name, this, checked);
    }

    Size measure(Painter& p, Size avail) override {
        float textw = label.empty() ? 0.f : p.measure_text(label, TextScale::Body).w;
        float w = or_default(hint_w, 40.f + (label.empty() ? 0.f : 14.f + textw));
        float h = or_default(hint_h, 32.f);
        (void)avail;
        return Size(w, h);
    }

    void paint(Painter& p) override {
        tick_anim();
        double t = now_ms();
        float hv = hover_t.at(t);
        float pv = press_t.at(t);
        float k  = knob_.at(t);
        if (knob_.running && host) host->mark_animating();

        const Palette& pal = p.palette();
        float tw = 40.f, th = 20.f;
        float cy = bounds.y + bounds.h * 0.5f;
        Rect track(bounds.x, cy - th * 0.5f, tw, th);

        // 关：中性灰面（比卡片亮一点，才看得见）；开：强调色。按 k 连续过渡，不在中点跳色
        Color off_col = mix(pal.control, pal.text, (pal.dark ? 0.13f : 0.07f) + hv * 0.04f);
        Color on_col  = state_color(pal.accent, pal.accent_hover, pal.accent_press, hv, pv);
        Color track_col;
        if (!enabled) {
            // 禁用态保持中性灰，不要残留强调色的蓝
            track_col = pal.layer;
        } else {
            track_col = mix(off_col, on_col, k);
        }
        // 开关也能开/关阴影：只有轨道投影，标签不投
        if (cast_shadow) p.shadow(track, th * 0.5f, 8.f, pal.shadow, 2.f);
        p.fill_round(track, th * 0.5f, track_col);
        if (k < 0.98f) {
            p.stroke_round(track.inset(0.5f), th * 0.5f,
                           mix(pal.control_stroke, Color(0, 0, 0, 0), k), 1.f);
        }

        // 拇指：直径接近轨道高度（留 2.5 边距），按下时轻微放大，位置用回弹缓动
        float inset = 2.5f;
        float r  = (th * 0.5f - inset) + pv * 1.f;
        float cx0 = track.x + inset + (th * 0.5f - inset);
        float cx1 = track.right() - inset - (th * 0.5f - inset);
        float cx = cx0 + (cx1 - cx0) * k;
        Color knob_col = enabled ? mix(pal.text_secondary, pal.on_accent, k) : pal.text_tertiary;
        p.fill_circle(Point(cx, cy), r, knob_col);

        if (!label.empty()) {
            Color tc = enabled ? pal.text : pal.text_tertiary;
            p.text(label, Rect(track.right() + 14.f, bounds.y, bounds.right() - track.right() - 14.f,
                               bounds.h),
                   TextScale::Body, tc, HAlign::Start, VAlign::Middle, false);
        }
    }

private:
    Tween knob_;
};

// 离屏截图用：>0 时环的"进场"从这么久之前开始算 —— 静态出图里才不会只画出一个点。
// 真窗口里保持 0，动画照常从 0 开始。
inline double& ring_preview_ms() { static double v = 0.0; return v; }
// 同理：出图时给不定态进度条一个时间偏移，才截得到游标在中间的那一帧。
inline double& bar_preview_ms() { static double v = 0.0; return v; }

// ---------------------------------------------------------- ProgressRing ----
class ProgressRing : public Widget {
public:
    float size = 32.f;
    bool  spinning = true;
    float value = 0.f;   // 0..1，spinning=false 时表示静态进度

    // 自动循环：自己从 loop_from 涨到 100%，到顶停留 loop_hold_ms 再回到 loop_from 继续（默认关）
    bool   auto_loop = false;
    double loop_ms   = 25000.0;   // 从 loop_from 走到 100% 的时间
    double loop_hold_ms = 0.0;    // 到 100% 之后停留多久再回 loop_from（0 = 立刻回）
    float  loop_from = 0.01f;     // 到 100% 之后回到哪一个值（1%）
    std::function<void(float)> on_loop;   // 整数百分比变了就回报一次（应用拿去更新文字）

    // 进出场时间（要改手感只动这里）
    static constexpr double kInMs   = 1200.0;   // 进场：太长头会"冲"一下，太短又会"啪"地蹦出来
    static constexpr double kOutMs  = 500.0;    // 收场：头保持原速，尾加速追上来
    static constexpr double kTurnMs = 1400.0;   // 转一圈的时间

    // 收起动画彻底走完（弧长已经归零）时回调 —— "然后触发接下来的动作"
    std::function<void()> on_finish;

    template <class... Args>
    explicit ProgressRing(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        size = or_default(o.height, 32.f);
        spinning = o.spinning;
        hint_w = o.width;
        hint_h = o.height;
    }

    Size measure(Painter& p, Size avail) override {
        (void)p;
        (void)avail;
        return Size(or_default(hint_w, size), or_default(hint_h, size));
    }

    void set_spinning(bool v) {
        spinning = v;
        if (host) host->request_repaint();
    }
    void set_value(float v) {
        value = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        if (host) host->request_repaint();
    }
    bool animating() const { return phase_ != Phase::Hidden; }

    // 自测用：当前这一帧画出来的头/尾角度（度，未折回）与阶段号 0=Hidden 1=In 2=Spin 3=Out
    float head_deg() const { return head_now_; }
    float tail_deg() const { return tail_now_; }
    int   phase_id() const { return (int)phase_; }

    // 一圈的弧长（比例）：不确定态是 300°，确定态就是 value
    float target_sweep_() const {
        if (!spinning) return value <= 0.f ? 0.f : (value > 1.f ? 1.f : value);
        return 300.f / 360.f;
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        if (auto_loop) {          // 自动循环：值自己涨，整数百分比每变一次回报一遍
            double cyc = loop_ms + loop_hold_ms;
            double tt  = cyc > 0.0 ? std::fmod(now_ms(), cyc) : 0.0;
            value = (tt >= loop_ms || loop_ms <= 0.0)
                        ? 1.f
                        : loop_from + (1.f - loop_from) * (float)(tt / loop_ms);
            int pct = (int)(value * 100.f + 0.5f);
            if (pct != last_pct_) {
                last_pct_ = pct;
                if (on_loop) on_loop(value);
                if (host) host->request_repaint();   // 只按 1% 的节拍重画，不用每帧出帧
            }
        }
        double now = now_ms();
        const bool want = spinning || value > 0.001f;

        const float kOmega  = 360.f / (float)kTurnMs;      // 稳态角速度（度/ms）
        const float kGapMax = target_sweep_() * 360.f;     // 稳态弧长（度）
        // 进场结束时"尾"已经走到的角度：尾用 u³ 加速，终点速度刚好 = ω → 3*kTailIn/kInMs = ω
        const float kTailIn = spin_tail_deg(kOmega);

        double dt = now - t_phase_;
        switch (phase_) {
            case Phase::Hidden:
                if (want) {
                    phase_ = Phase::In;
                    t_phase_ = now - ring_preview_ms();   // 出图时可以"跳"到进场的某一刻
                    rot_moving_ = false;
                    dt = now - t_phase_;
                }
                else return;
                break;
            case Phase::In:
                if (!want) { enter_out(now); dt = 0.0; break; }
                if (dt >= kInMs) {
                    phase_ = Phase::Spin; t_phase_ = now;
                    tail_spin_ = spinning ? kTailIn : 0.f;
                    rot_moving_ = spinning;
                    dt = 0.0;
                }
                break;
            case Phase::Spin:
                if (!want) { enter_out(now); dt = 0.0; }
                else if (spinning) rot_moving_ = true;   // 记下"这个环是会转的"
                break;
            case Phase::Out:
                if (want) {          // 中途又叫它开始：从当前的尾角度接着转
                    phase_ = Phase::Spin; t_phase_ = now;
                    tail_spin_ = tail_now_;
                    rot_moving_ = spinning;
                    dt = 0.0;
                } else if (dt >= kOutMs) {
                    phase_ = Phase::Hidden;
                    tail_now_ = head_now_ = 0.f;
                    if (host) host->request_repaint();
                    if (on_finish) on_finish();
                    return;
                }
                break;
        }

        // ---- 本帧的「尾角度」和「弧长」：所有分支都保证上一阶段的终点 = 下一阶段的起点
        float tail = 0.f, gap = 0.f;
        if (phase_ == Phase::In && !spinning) {
            // 确定态进场：尾巴钉死在 0（它根本不转），弧长从 0 长到 value。
            // 不能借用不定态那套"头先跑、尾追上来"的曲线 —— 那套是按 ω 收尾的，
            // 进稳态时尾巴会从 tail_end 直接归 0，整段弧瞬移一下，看着就像"先当了会儿不确定环"。
            float u = kInMs > 0.0 ? (float)(dt / kInMs) : 1.f;
            if (u > 1.f) u = 1.f;
            float e = u * u * (3.f - 2.f * u);   // ease-in-out：起步慢、中间快、收尾慢
            gap = kGapMax * e;
        } else if (phase_ == Phase::In) {
            // 起步：头尾先重合（"一个点"），头先走开、而且是从静止加速出去；
            // 尾晚一点（u1）才起步，也用"零初速"的方式加到自己跟上稳态角速度。
            // 两条曲线的**末速度都等于 ω**、末端弧长正好等于稳态弧长，所以切进 Spin 不跳、不顿。
            float u = kInMs > 0.0 ? (float)(dt / kInMs) : 1.f;
            if (u > 1.f) u = 1.f;
            const float u1 = kTailStart;                     // 尾什么时候开始动（比例）
            float kIn2 = (float)kInMs * (1.f - u1);
            float tail_end = kOmega * kIn2 / 3.f;            // 尾起步段扫过的角度（末速 = ω）
            float head_end = kGapMax + tail_end;             // 头要走到"稳态弧长 + 尾的落后量"
            // 头：h(u) = head_end * (p u² + q u³)，h(1)=1、h'(0)=0、h'(1)=ω*kIn/head_end
            float p = 2.5f, q = -1.5f;
            if (head_end > 0.f) {
                float slope = kOmega * (float)kInMs / head_end;   // 末端斜率（归一化）
                p = 3.f - slope;
                q = slope - 2.f;
            }
            float hh = p * u * u + q * u * u * u;
            if (hh < 0.f) hh = 0.f;
            float head = head_end * hh;
            float s = u > u1 ? (u - u1) / (1.f - u1) : 0.f;
            tail = tail_end * s * s * s;                     // 尾：零初速、末速 ω
            gap = head - tail;
            if (gap < 1.2f) gap = 1.2f;                      // 起步那一下留个"点"，别整帧空白
        } else if (phase_ == Phase::Spin) {
            // 稳态：头尾同速（ω）一起转，弧长轻微呼吸
            if (spinning) tail = tail_spin_ + kOmega * (float)dt;
            // 呼吸从 1.0 开始（0.94+0.06cos，起步正好 1.0），否则进场切稳态会突然短一截
            float breathe = spinning ? (0.94f + 0.06f * (float)std::cos((float)dt / 520.f)) : 1.f;
            gap = kGapMax * breathe;
        } else {   // Out
            float u = kOutMs > 0.0 ? (float)(dt / kOutMs) : 1.f;
            if (u > 1.f) u = 1.f;
            // 头保持原速（ω）继续往前；尾按 u² 加速追上来，gap 在 0.5s 内正好归零。
            // 注意：头**永远**按 ω 走，不能因为"还没进稳态"就冻住——进场没走完就被关掉时，
            // 冻住头会变成"头停着、只有尾在追"，看起来就是动画从反方向收。
            float head = head_out_ + kOmega * (float)dt;
            gap  = gap_out_ * (1.f - u * u);
            tail = head - gap;
        }

        tail_now_ = tail;
        head_now_ = tail + gap;

        bool moving = phase_ == Phase::In || phase_ == Phase::Out ||
                      (phase_ == Phase::Spin && spinning);
        if (host && moving) host->mark_animating();

        float d = size < bounds.h ? size : bounds.h;
        if (d > bounds.w) d = bounds.w;
        Point c(bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f);
        float sw = 2.5f;
        float rad = d * 0.5f - sw;

        if (gap <= 0.001f) return;   // 头和尾已经重合（收干净了）
        // 360° 的弧在 D2D 里会当成 0°（画出来是空的），留 0.5° 缝：视觉上看不出，满格不会凭空消失
        if (gap > 359.5f) gap = 359.5f;

        p.arc(c, rad, tail, gap, sw, pal.accent);
    }

    // 进场结束时尾已经扫过的角度（让尾的末速度正好等于 ω，接上稳态不跳）
    static float spin_tail_deg(float omega) {
        // 尾在 kInMs 的 (1-kTailStart) 段里加速，末速 = ω → 扫过 ω*kIn2/3
        return omega * (float)kInMs * (1.f - kTailStart) / 3.f;
    }
    static constexpr float kTailStart = 0.35f;   // 进场里"尾"开始动的时刻（比例）

private:
    enum class Phase { Hidden, In, Spin, Out };
    Phase  phase_ = Phase::Hidden;
    double t_phase_ = 0.0;        // 本阶段起点
    float  tail_spin_ = 0.f;      // 进稳态那一刻的尾角度
    bool   rot_moving_ = false;   // 这个环是会转的（不确定态）；确定态不转
    float  gap_out_  = 0.f;       // 开始收场时的弧长
    float  head_out_ = 0.f;       // 开始收场时头所在的角度（之后按 ω 继续走）
    float  tail_now_ = 0.f;       // 上一帧画出来的尾角（中途打断时接着用）
    float  head_now_ = 0.f;
    int    last_pct_ = -1;        // auto_loop：上一次回报的整数百分比

    void enter_out(double now) {
        phase_ = Phase::Out;
        t_phase_ = now;
        gap_out_  = head_now_ - tail_now_;
        head_out_ = head_now_;
    }
};

// ---------------------------------------------------------- ProgressBar ----
// 细进度条（Win11 形状：默认 4px 厚、胶囊圆角、槽是 track、填充是强调色）。
// 条里**没有任何文字**：要显示百分比/说明，调用方自己放 label 在旁边。
class ProgressBar : public Widget {
public:
    float value = 0.35f;      // 0..1（超出自动夹住）
    bool  indeterminate = false;
    float thickness = 4.f;
    float radius    = -1.f;   // < 0 → 胶囊（thickness / 2）
    double sweep_ms = 1600.0; // 不定态走完一趟的时间
    Color track_color{0.f, 0.f, 0.f, 0.f};   // alpha = 0 → 用主题的 pal.track
    Color fill_color{0.f, 0.f, 0.f, 0.f};    // alpha = 0 → 用主题的 pal.accent

    // 自动循环：自己从 loop_from 涨到 100%，到顶停留 loop_hold_ms 再回到 loop_from 继续（默认关）
    bool   auto_loop = false;
    double loop_ms   = 25000.0;   // 从 loop_from 走到 100% 的时间
    double loop_hold_ms = 0.0;    // 到 100% 之后停留多久再回 loop_from（0 = 立刻回）
    float  loop_from = 0.01f;     // 到 100% 之后回到哪一个值（1%）
    std::function<void(float)> on_loop;   // 整数百分比变了就回报一次（应用拿去更新文字）
    int    last_pct  = -1;

    template <class... Args>
    explicit ProgressBar(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        if (!std::isnan(o.progress)) value = o.progress;
        indeterminate = o.indeterminate;
        if (!std::isnan(o.thickness)) thickness = o.thickness;
        if (!std::isnan(o.radius)) radius = o.radius;
        flex = o.flex;
        hint_w = o.width;
        hint_h = o.height;
        if (std::isnan(hint_h) || hint_h <= 0.f) hint_h = thickness;
    }

    Size measure(Painter& p, Size avail) override {
        (void)p;
        (void)avail;
        // 不自己报 avail.w：那样会把父容器的整条宽度当成"自然宽度"，一进 flex 行就撑破。
        // 宽度默认交给 flex / stretch 分配（给了 kw::width 才用给的）。
        return Size(or_default(hint_w, 0.f), or_default(hint_h, thickness));
    }

    void set_value(float v) {
        value = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        if (host) host->request_repaint();
    }
    void set_indeterminate(bool v) {
        indeterminate = v;
        if (host) host->request_repaint();
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        if (auto_loop) {          // 自动循环：值自己涨，整数百分比每变一次回报一遍
            double cyc = loop_ms + loop_hold_ms;
            double tt  = cyc > 0.0 ? std::fmod(now_ms(), cyc) : 0.0;
            value = (tt >= loop_ms || loop_ms <= 0.0)
                        ? 1.f
                        : loop_from + (1.f - loop_from) * (float)(tt / loop_ms);
            int pct = (int)(value * 100.f + 0.5f);
            if (pct != last_pct) {
                last_pct = pct;
                if (on_loop) on_loop(value);
                if (host) host->request_repaint();   // 只按 1% 的节拍重画，不用每帧出帧
            }
        }
        float h = thickness < bounds.h ? thickness : bounds.h;
        float y = bounds.y + (bounds.h - h) * 0.5f;
        float r = radius < 0.f ? h * 0.5f : radius;
        Color tr = track_color.a > 0.f ? track_color : pal.track;
        Color fc = fill_color.a > 0.f ? fill_color : pal.accent;

        p.fill_round(Rect(bounds.x, y, bounds.w, h), r, tr);

        if (indeterminate) {
            double t = std::fmod(now_ms() + bar_preview_ms(), sweep_ms) / sweep_ms;   // 0..1 循环
            // 形状：从左边进来时逐渐拉长、到中间最长、出去时缩回去 —— Win11 的不定态就是这样
            float u = (float)t;
            float swell = u < 0.5f ? u * 2.f : (1.f - u) * 2.f;     // 0→1→0
            swell = 1.f - (1.f - swell) * (1.f - swell);            // ease-out，起步就长开
            float seg = bounds.w * (0.18f + 0.34f * swell);
            if (seg < h) seg = h;
            float cx = -seg * 0.5f + (bounds.w + seg) * u;          // 游标中心
            float x = cx - seg * 0.5f;
            // 出入两端的 10% 里淡入淡出，别让它硬生生从边上冒出来
            float fade = 1.f;
            if (x < 0.f) fade = 1.f + x / seg;                      // 左边界外
            if (x + seg > bounds.w) fade = (bounds.w - x) / seg;    // 右边界外
            if (fade < 0.f) fade = 0.f;
            if (fade > 1.f) fade = 1.f;
            Color c = fc;
            c.a *= fade;
            // 裁到槽的矩形里：游标是从槽外面"进来"的，不裁就会画到条外面去（左侧最明显）
            p.push_clip(Rect(bounds.x, y, bounds.w, h));
            p.fill_round(Rect(bounds.x + x, y, seg, h), r, c);
            p.pop_clip();
            if (host) host->mark_animating();                       // 不定态要一直在出帧
        } else {
            float v = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
            float w = bounds.w * v;
            if (w > 0.f) {
                if (w < h) w = h;                                   // 太短会画不出胶囊，留一个圆点
                p.fill_round(Rect(bounds.x, y, w, h), r, fc);
            }
        }
    }

};

// -------------------------------------------------------------- Divider ----
class Divider : public Widget {
public:
    template <class... Args>
    explicit Divider(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        hint_h = or_default(o.height, 1.f);
    }
    Size measure(Painter& p, Size avail) override {
        (void)p;
        return Size(avail.w, or_default(hint_h, 1.f));
    }
    void paint(Painter& p) override {
        p.fill_rect(Rect(bounds.x, bounds.y + bounds.h * 0.5f - 0.5f, bounds.w, 1.f),
                    p.palette().divider);
    }
};

// --------------------------------------------------------------- Spacer ----
class Spacer : public Widget {
public:
    template <class... Args>
    explicit Spacer(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        hint_h = or_default(o.height, 8.f);
    }
    Size measure(Painter& p, Size avail) override {
        (void)p;
        (void)avail;
        return Size(or_default(hint_w, 0.f), or_default(hint_h, 8.f));
    }
};

// ---------------------------------------------------------------- Stack ----
// 线性布局容器（默认纵向）。spacing 为子项间距，max_width 让内容列居中。
class Stack : public Widget {
public:
    std::vector<std::shared_ptr<Widget>> children;
    float spacing = 12.f;
    float padding = 0.f;
    float max_content_width = NAN;
    bool  horizontal = false;
    bool  stretch_children = true;

    template <class... Args>
    explicit Stack(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        spacing = or_default(o.spacing, 12.f);
        padding = or_default(o.padding, 0.f);
        max_content_width = o.max_width;
        hint_w = o.width;
        hint_h = o.height;
        flex = o.flex;
        stretch_children = o.stretch;
    }

    std::shared_ptr<Stack> add(std::shared_ptr<Widget> w) {
        if (!w) return std::static_pointer_cast<Stack>(shared_from_this());
        if (host) w->attach(host);
        children.push_back(std::move(w));
        if (host) {
            host->request_repaint();
        }
        return std::static_pointer_cast<Stack>(shared_from_this());
    }

    void on_attach() override {
        for (auto& c : children) c->attach(host);
    }

    Size measure(Painter& p, Size avail) override {
        float pad = padding;
        float w = avail.w;
        if (!std::isnan(hint_w)) w = hint_w;
        float inner_w = (horizontal ? 1e6f : w) - pad * 2.f;
        if (!horizontal && !std::isnan(max_content_width) && inner_w > max_content_width)
            inner_w = max_content_width;

        float total = pad * 2.f;
        float maxw = 0.f;
        for (size_t i = 0; i < children.size(); ++i) {
            Size cs = children[i]->measure(p, Size(horizontal ? 1e6f : inner_w, 1e6f));
            if (horizontal) {
                float cw = std::isnan(children[i]->hint_w) ? cs.w : children[i]->hint_w;
                total += cw;
                maxw = std::max(maxw, std::max(cs.h, or_default(children[i]->hint_h, 0.f)));
            } else {
                total += std::isnan(children[i]->hint_h) ? cs.h : children[i]->hint_h;
                maxw = std::max(maxw, cs.w);
            }
            if (i + 1 < children.size()) total += spacing;
        }
        if (horizontal) return Size(or_default(hint_w, total), or_default(hint_h, std::max(maxw, pad)));
        return Size(or_default(hint_w, maxw + pad * 2.f), or_default(hint_h, total));
    }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        if (horizontal) {
            arrange_row(p, r);
        } else {
            arrange_column(p, r);
        }
    }

    Widget* hit_test(Point pt) override {
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (Widget* w = (*it)->hit_test(pt)) return w;
        }
        if (interactive() && bounds.contains(pt)) return this;
        return nullptr;
    }

    void paint(Painter& p) override {
        tick_anim();
        for (auto& c : children) c->paint(p);
    }

private:
    void content_box(const Rect& r, float& x, float& w) {
        float avail = r.w - padding * 2.f;
        w = avail;
        x = r.x + padding;
        if (!std::isnan(max_content_width) && avail > max_content_width) {
            w = max_content_width;
            x = r.x + (r.w - w) * 0.5f;   // 内容列居中
        }
    }

    void arrange_column(Painter& p, const Rect& r) {
        float x = 0.f, cw = 0.f;
        content_box(r, x, cw);
        size_t n = children.size();
        if (n == 0) return;

        std::vector<Size> cs(n);
        float fixed = 0.f, fl = 0.f;
        for (size_t i = 0; i < n; ++i) {
            cs[i] = children[i]->measure(p, Size(cw, r.h - padding * 2.f));
            fixed += std::isnan(children[i]->hint_h) ? cs[i].h : children[i]->hint_h;
            fl += std::max(0.f, children[i]->flex);
        }
        float gaps = spacing * (float)(n - 1);
        float leftover = r.h - padding * 2.f - fixed - gaps;

        float y = r.y + padding;
        for (size_t i = 0; i < n; ++i) {
            float h = std::isnan(children[i]->hint_h) ? cs[i].h : children[i]->hint_h;
            if (leftover > 0.f && fl > 0.f) h += leftover * (std::max(0.f, children[i]->flex) / fl);
            float wchild = cw;
            if (!stretch_children && std::isnan(children[i]->hint_w)) wchild = std::min(cw, cs[i].w);
            children[i]->arrange(p, Rect(x, y, wchild, h));
            y += h + spacing;
        }
    }

    void arrange_row(Painter& p, const Rect& r) {
        size_t n = children.size();
        if (n == 0) return;
        float inner_w = r.w - padding * 2.f;

        std::vector<Size> cs(n);
        std::vector<float> want(n, 0.f);
        float fixed = 0.f, fl = 0.f;
        for (size_t i = 0; i < n; ++i) {
            cs[i] = children[i]->measure(p, Size(1e6f, r.h - padding * 2.f));
            want[i] = std::isnan(children[i]->hint_w) ? cs[i].w : children[i]->hint_w;
            fixed += want[i];
            fl += std::max(0.f, children[i]->flex);
        }
        float gaps = spacing * (float)(n - 1);
        float leftover = inner_w - fixed - gaps;

        // flex 子项按权重平分「非弹性子项占完之后」的空间：
        // flex(1) 的两张卡就是等宽，而且不管内容测得多宽都不会把行撑出窗口。
        float nonflex = 0.f;
        for (size_t i = 0; i < n; ++i)
            if (children[i]->flex <= 0.f) nonflex += want[i];
        float flex_room = inner_w - gaps - nonflex;
        std::vector<float> wv(n);
        for (size_t i = 0; i < n; ++i) {
            if (fl > 0.f && children[i]->flex > 0.f && flex_room > 0.f)
                wv[i] = flex_room * (children[i]->flex / fl);
            else
                wv[i] = want[i];
        }

        float x = r.x + padding;
        for (size_t i = 0; i < n; ++i) {
            float w = wv[i];
            if (w < 0.f) w = 0.f;
            float h = std::isnan(children[i]->hint_h) ? cs[i].h : children[i]->hint_h;
            if (h > r.h) h = r.h;
            // 行内默认把子项拉到同一高度（并排的两张卡要齐平），关掉 stretch 才按内容居中
            float y = stretch_children ? r.y : r.y + (r.h - h) * 0.5f;
            if (stretch_children) h = r.h;
            children[i]->arrange(p, Rect(x, y, w, h));
            x += w + spacing;
        }
    }
};

// ----------------------------------------------------------------- Card ----
class Card : public Widget {
public:
    std::wstring title;
    std::wstring subtitle;
    std::shared_ptr<Stack> body;
    float padding = 18.f;
    float radius = Radius::card;
    bool  glass = false;     // kw::acrylic()：磨砂面（亚克力观感的近似），默认关

    float shadow_radius() const override { return radius; }
    float shadow_blur() const override { return 4.5f; }
    float shadow_offset() const override { return 2.5f; }

    template <class... Args>
    explicit Card(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        title = o.title;
        subtitle = o.subtitle;
        padding = or_default(o.padding, 18.f);
        radius = or_default(o.radius, Radius::card);
        hint_w = o.width;
        hint_h = o.height;
        flex = o.flex;
        cast_shadow = shadow_opt(o, true);   // 卡片默认投影，kw::shadow(false) 关掉
        glass = o.acrylic > 0;               // 磨砂面要显式 kw::acrylic()，默认还是实色
        body = std::make_shared<Stack>(kw::spacing(o.spacing), kw::padding(0.f), kw::stretch(o.stretch));
    }

    std::shared_ptr<Card> add(std::shared_ptr<Widget> w) {
        body->add(std::move(w));
        if (host) body->attach(host);
        return std::static_pointer_cast<Card>(shared_from_this());
    }

    void on_attach() override { body->attach(host); }

    Size measure(Painter& p, Size avail) override {
        float inner = (std::isnan(hint_w) ? avail.w : hint_w) - padding * 2.f;
        Size b = body->measure(p, Size(inner, 1e6f));
        float h = padding * 2.f + b.h + header_height(p);
        return Size(or_default(hint_w, std::min(avail.w, b.w + padding * 2.f)),
                    or_default(hint_h, h));
    }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        float top = bounds.y + padding + header_height(p);
        float body_h = bounds.bottom() - padding - top;
        if (body_h < 0.f) body_h = 0.f;
        body->arrange(p, Rect(bounds.x + padding, top, bounds.w - padding * 2.f, body_h));
    }

    Widget* hit_test(Point pt) override {
        if (!bounds.contains(pt)) return nullptr;
        if (Widget* w = body->hit_test(pt)) return w;
        return nullptr;
    }

    void paint(Painter& p) override {
        tick_anim();
        const Palette& pal = p.palette();
        draw_shadow(p);   // kw::shadow(false) 关掉；半径/模糊走基类虚函数
        if (glass) p.fill_glass(bounds, radius);            // kw::acrylic()：磨砂面
        else p.fill_round(bounds, radius, pal.card);
        if (!glass) p.stroke_round(bounds.inset(0.5f), radius, pal.stroke, 1.f);

        float y = bounds.y + padding;
        if (!title.empty()) {
            p.text(title, Rect(bounds.x + padding, y, bounds.w - padding * 2.f,
                               p.line_height(TextScale::Subtitle)), TextScale::Subtitle, pal.text,
                   HAlign::Start, VAlign::Middle, false, true);
        }
        if (!subtitle.empty()) {
            float tsh = p.line_height(TextScale::Caption);
            p.text(subtitle,
                   Rect(bounds.x + padding, y + p.line_height(TextScale::Subtitle) + 2.f,
                        bounds.w - padding * 2.f, tsh),
                   TextScale::Caption, pal.text_secondary, HAlign::Start, VAlign::Middle);
        }
        body->paint(p);
    }

private:
    float header_height(Painter& p) {
        float h = 0.f;
        if (!title.empty()) h += p.line_height(TextScale::Subtitle) + 2.f;
        if (!subtitle.empty()) h += p.line_height(TextScale::Caption) + 2.f;
        if (h > 0.f) h += 12.f;   // 标题与正文间距
        return h;
    }
};

// 控件里画的图标字符（默认是 Segoe Fluent Icons 的码位）。
// 库不把图标字符写死：换了图标字体，或者想用普通字符（"√" "▸" "⌄"），
// 就 w11ui::Glyphs g; g.check = L"√"; w11ui::set_glyphs(g);
struct Glyphs {
    std::wstring check        = L"\uE73E";   // 菜单选中
    std::wstring submenu      = L"\uE76C";   // 这一项有子菜单
    std::wstring chevron      = L"\uE70D";   // 下拉收起态箭头
    std::wstring chevron_open = L"\uE70E";   // 下拉展开态箭头
};
inline Glyphs& glyphs() { static Glyphs g; return g; }
inline void set_glyphs(const Glyphs& g) { glyphs() = g; }

// ------------------------------------------------------------ 日期工具 ----
// 库不内置任何文案：星期名、月份名、按钮字全部从外面传；这里只有算术。
inline bool is_leap_year(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

inline int days_in_month(int y, int m) {
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 30;
    if (m == 2) return is_leap_year(y) ? 29 : 28;
    return d[m - 1];
}

// 某天是星期几：0 = 周日（Sakamoto 算法，不碰 CRT 的时区/夏令时）
inline int weekday_of(int y, int m, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int yy = y;
    if (m < 3) yy -= 1;
    int w = (yy + yy / 4 - yy / 100 + yy / 400 + t[(m < 1 || m > 12) ? 0 : m - 1] + d) % 7;
    return w < 0 ? w + 7 : w;
}

inline std::wstring pad_num(int v, int width) {
    std::wstring s = std::to_wstring(v);
    while ((int)s.size() < width) s.insert(s.begin(), L'0');
    return s;
}

// 按格式串填日期。令牌：yyyy（四位年）、yy（两位年）、MM（两位月）、M（月）、
// dd（两位日）、d（日）、MMM（月份名，要 months 非空）；其余字符原样输出。
// 默认 yyyy-MM-dd，也就是程序里说的"4y-2m-2d"。
inline std::wstring format_date(int y, int m, int d, const std::wstring& pattern,
                                const std::vector<std::wstring>* months = nullptr) {
    if (pattern.empty()) return pad_num(y, 4) + L"-" + pad_num(m, 2) + L"-" + pad_num(d, 2);
    std::wstring out;
    for (std::size_t i = 0; i < pattern.size();) {
        wchar_t c = pattern[i];
        std::size_t n = 1;
        while (i + n < pattern.size() && pattern[i + n] == c) ++n;
        if (c == L'y') {
            out += (n >= 4) ? pad_num(y, 4) : pad_num(((y % 100) + 100) % 100, 2);
        } else if (c == L'M') {
            if (n >= 3 && months && (int)months->size() >= 12)
                out += (*months)[(std::size_t)((m - 1) % 12)];
            else
                out += pad_num(m, n >= 2 ? 2 : 1);
        } else if (c == L'd') {
            out += pad_num(d, n >= 2 ? 2 : 1);
        } else {
            out.append(n, c);
        }
        i += n;
    }
    return out;
}

// 从文本按同一个格式串反解日期。数字按令牌出现的顺序取，其它字符当分隔符。
inline bool parse_date(const std::wstring& text, const std::wstring& pattern,
                       int& y, int& m, int& d) {
    y = m = d = 0;
    if (text.empty()) return false;
    std::wstring pat = pattern.empty() ? std::wstring(L"yyyy-MM-dd") : pattern;

    std::vector<wchar_t> toks;                        // 令牌类型序列（y / M / d）
    std::vector<int>     widths;                      // 每个令牌要几位（0 = 不补零）
    for (std::size_t i = 0; i < pat.size();) {
        wchar_t c = pat[i];
        std::size_t n = 1;
        while (i + n < pat.size() && pat[i + n] == c) ++n;
        if (c == L'y' || c == L'M' || c == L'd') { toks.push_back(c); widths.push_back((int)n); }
        i += n;
    }
    if (toks.empty()) return false;

    std::vector<int> nums;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] >= L'0' && text[i] <= L'9') {
            int v = 0;
            while (i < text.size() && text[i] >= L'0' && text[i] <= L'9') {
                v = v * 10 + (int)(text[i] - L'0');
                ++i;
                if (v > 99999) return false;
            }
            nums.push_back(v);
        } else {
            ++i;
        }
    }
    if (nums.size() < toks.size()) return false;

    for (std::size_t k = 0; k < toks.size(); ++k) {
        int v = nums[k];
        if (toks[k] == L'y') {
            if (widths[k] <= 2 && v < 100) v += (v < 70 ? 2000 : 1900);   // yy 的两位年
            y = v;
        } else if (toks[k] == L'M') {
            m = v;
        } else {
            d = v;
        }
    }
    if (y < 1 || m < 1 || m > 12 || d < 1 || d > days_in_month(y, m)) {
        y = m = d = 0;
        return false;
    }
    if (y > 9999) { y = m = d = 0; return false; }
    return true;
}

// --------------------------------------------------------------- Rating ----
// 星级评分：点第 N 颗就把值设成 N，鼠标悬浮时预览到光标那一颗。
// 星形字形由调用方给（默认是 Segoe Fluent Icons 的实心/空心星）。
class Rating : public Widget {
public:
    int   value = 0;
    int   max_stars = 5;
    float star_size = 20.f;
    float gap = 4.f;
    std::wstring glyph_on  = L"\uE735";   // FavoriteStarFill
    std::wstring glyph_off = L"\uE734";   // FavoriteStar
    std::function<void(int)> on_change;

    template <class... Args>
    explicit Rating(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        value = o.rating;
        max_stars = o.rating_max < 1 ? 5 : o.rating_max;
        star_size = or_default(o.width, 20.f);   // 用 width 当单颗星尺寸？不：用高度更自然
        if (!std::isnan(o.height)) star_size = o.height;
        change_name = o.change;
        click_name = o.click;
        enabled = o.enabled;
        if (o.width > 0.f) total_w_ = o.width;
    }

    void set_value(int v) {
        if (v < 0) v = 0;
        if (v > max_stars) v = max_stars;
        value = v;
        if (host) host->request_repaint();
        if (on_change) on_change(value);
        if (host && !change_name.empty()) host->fire(change_name, this, value > 0);
    }

    Size measure(Painter& p, Size avail) override {
        (void)p; (void)avail;
        float w = total_w_ > 0.f ? total_w_ : (star_size * (float)max_stars + gap * (float)(max_stars - 1));
        return Size(w, star_size);
    }

    void paint(Painter& p) override {
        tick_anim();
        const Palette& pal = p.palette();
        int shown = hover_star_ > 0 ? hover_star_ : value;
        for (int i = 0; i < max_stars; ++i) {
            float x = bounds.x + (star_size + gap) * (float)i;
            Rect r(x, bounds.y, star_size, star_size);
            bool on = (i < shown);
            Color c = on ? (enabled ? pal.accent : pal.text_tertiary)
                         : (enabled ? pal.text_tertiary : pal.control_stroke);
            p.glyph(on ? glyph_on : glyph_off, r, star_size * 0.86f, c);
        }
    }

    Widget* hit_test(Point pt) override {
        if (enabled && bounds.contains(pt)) return this;
        return nullptr;
    }
    int star_at(Point pt) const {
        float x = pt.x - bounds.x;
        if (x < 0.f) return 0;
        int i = (int)(x / (star_size + gap)) + 1;
        return i < 1 ? 1 : (i > max_stars ? max_stars : i);
    }
    void on_move(Point pt) override {
        int s = star_at(pt);
        if (s != hover_star_) { hover_star_ = s; if (host) host->request_repaint(); }
    }
    void on_hover(bool on) override {
        Widget::on_hover(on);
        if (!on) { hover_star_ = 0; if (host) host->request_repaint(); }
    }
    void on_click_at(Point pt) override {
        set_value(star_at(pt));
    }

    float total_w_ = 0.f;
    int hover_star_ = 0;
};

// ---------------------------------------------------------------- Badge ----
// 小标签：一段文字 + 一层淡色胶囊底。颜色跟着 Variant 走（默认就是中性灰）。
class Badge : public Widget {
public:
    std::wstring text;
    int tone = 0;      // 0 中性 / 1 成功 / 2 警告 / 3 错误 / 4 强调色
    TextScale scale = TextScale::Caption;

    template <class... Args>
    explicit Badge(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        text = o.text.empty() ? o.title : o.text;
        tone = (o.severity < 0 || o.severity > 4) ? 0 : o.severity;
        if (o.variant == Variant::Primary) tone = 4;
        scale = o.scale;
        hint_h = o.height;
        enabled = o.enabled;
    }

    void set_text(const std::wstring& t) {
        text = t;
        if (host) host->request_repaint();
    }

    Size measure(Painter& p, Size avail) override {
        (void)avail;
        float tw = text.empty() ? 0.f : p.measure_text(text, scale).w;
        return Size(or_default(hint_w, tw + 16.f), or_default(hint_h, 20.f));
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        Color bg, fg;
        switch (tone) {
            case 1:  bg = Color(0.18f, 0.62f, 0.34f, 0.16f); fg = Color(0.30f, 0.75f, 0.45f); break;
            case 2:  bg = Color(0.90f, 0.70f, 0.20f, 0.18f); fg = Color(0.92f, 0.74f, 0.26f); break;
            case 3:  bg = Color(0.78f, 0.17f, 0.15f, 0.16f); fg = Color(0.86f, 0.29f, 0.25f); break;
            case 4:  bg = pal.accent.alpha(0.16f);           fg = pal.accent; break;
            default: bg = pal.control.alpha(pal.dark ? 0.28f : 0.50f); fg = pal.text_secondary;
        }
        if (!enabled) { bg = pal.control.alpha(0.15f); fg = pal.text_tertiary; }
        p.fill_round(bounds, bounds.h * 0.5f, bg);
        p.text(text, bounds, scale, fg, HAlign::Center, VAlign::Middle, false);
    }
};

// -------------------------------------------------------------- InfoBar ----
// 提示条：左边一个状态图标、中间标题 + 正文、右边一个关闭钮。
// 四种状态只影响图标和配色，文案全部由调用方给。
class InfoBar : public Widget {
public:
    enum class Severity { Info, Success, Warning, Error };

    std::wstring title;
    std::wstring message;
    Severity severity = Severity::Info;
    bool closable = false;
    std::wstring glyph_info    = L"\uE946";   // Info
    std::wstring glyph_success = L"\uE930";   // Completed
    std::wstring glyph_warning = L"\uE7BA";   // Warning
    std::wstring glyph_error   = L"\uEA39";   // ErrorBadge
    float close_w = 32.f;

    template <class... Args>
    explicit InfoBar(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        title = o.title;
        message = o.text;
        severity = (Severity)(o.severity < 0 || o.severity > 3 ? 0 : o.severity);
        closable = o.closable;
        click_name = o.click;
        change_name = o.change;
        hint_h = o.height;
        enabled = o.enabled;
        hint_w = o.width;
        flex = o.flex;
    }

    void on_click_at(Point pt) override {
        if (closable && pt.x > bounds.right() - close_w) {
            visible_ = false;
            if (host) { host->request_repaint(); if (!click_name.empty()) host->fire(click_name, this, false); }
            return;
        }
    }
    bool interactive() const override { return enabled && closable; }

    Size measure(Painter& p, Size avail) override {
        (void)avail;
        float w = title.empty() ? 0.f : p.measure_text(title, TextScale::BodyStrong).w;
        if (!message.empty()) w += p.measure_text(message, TextScale::Caption).w + 8.f;
        return Size(or_default(hint_w, w + 76.f), or_default(hint_h, 44.f));
    }

    void paint(Painter& p) override {
        if (!visible_) return;
        const Palette& pal = p.palette();
        Color accent, tint;
        std::wstring ico;
        switch (severity) {
            case Severity::Success: accent = Color(0.22f, 0.68f, 0.42f); ico = glyph_success; break;
            case Severity::Warning: accent = Color(0.90f, 0.70f, 0.20f); ico = glyph_warning; break;
            case Severity::Error:   accent = Color(0.85f, 0.28f, 0.24f); ico = glyph_error;   break;
            default:                accent = pal.accent;                 ico = glyph_info;    break;
        }
        tint = accent.alpha(pal.dark ? 0.14f : 0.10f);
        p.fill_round(bounds, Radius::control, tint);
        p.stroke_round(bounds.inset(0.5f), Radius::control, accent.alpha(0.35f), 1.f);

        float cy = bounds.y + bounds.h * 0.5f;
        p.glyph(ico, Rect(bounds.x + 12.f, cy - 9.f, 18.f, 18.f), 15.f, accent);
        float tx = bounds.x + 40.f;
        float tw = bounds.w - 40.f - (closable ? close_w : 12.f);
        if (!title.empty()) {
            p.text(title, Rect(tx, bounds.y, tw, bounds.h), TextScale::BodyStrong,
                   pal.text, HAlign::Start, VAlign::Middle, false);
            tx += p.measure_text(title, TextScale::BodyStrong).w + 8.f;
        }
        if (!message.empty()) {
            float mw = bounds.right() - (closable ? close_w : 12.f) - tx;
            if (mw > 8.f)
                p.text(message, Rect(tx, bounds.y, mw, bounds.h), TextScale::Caption,
                       pal.text_secondary, HAlign::Start, VAlign::Middle, false);
        }
        if (closable) {
            Rect cb(bounds.right() - close_w, bounds.y, close_w, bounds.h);
            if (hover_close_) p.fill_round(cb.inset(4.f), Radius::control,
                                           pal.dark ? Color(1,1,1,0.06f) : Color(0,0,0,0.04f));
            p.glyph(L"\uE711", Rect(cb.x + cb.w * 0.5f - 6.f, cy - 6.f, 12.f, 12.f), 10.f,
                    pal.text_secondary);
        }
    }
    void on_move(Point pt) override { hover_close_ = closable && pt.x > bounds.right() - close_w; }

    bool visible_ = true;
    bool hover_close_ = false;
};

// -------------------------------------------------------------- Expander ----
// 可折叠面板：一行标题（点它展开/收起），下面挂任意控件。
// 展开高度带 200ms 过渡；收起时内容先被裁掉再让位。
class Expander : public Stack {
public:
    std::wstring title;
    std::wstring subtitle;
    std::wstring glyph_open  = L"\uE70D";   // ChevronDown：展开
    std::wstring glyph_close = L"\uE76C";   // ChevronRight：收起
    bool  expanded = false;
    float header_h = 40.f;

    template <class... Args>
    explicit Expander(Args&&... args) : Stack(std::forward<Args>(args)...) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        title = o.title;
        subtitle = o.subtitle;
        expanded = o.expanded;
        ease_ = expanded ? 1.f : 0.f;
        change_name = o.change;
        click_name = o.click;
        spacing = or_default(o.spacing, 8.f);
        enabled = o.enabled;
    }

    bool interactive() const override { return enabled; }

    void set_expanded(bool on, bool notify = true) {
        if (expanded == on) return;
        expanded = on;
        anim_t0_ = now_ms();
        anim_from_ = ease_;
        if (host) { host->request_repaint(); host->mark_animating(); }
        if (notify) {
            if (!change_name.empty() && host) host->fire(change_name, this, expanded);
            if (!click_name.empty() && host) host->fire(click_name, this, expanded);
        }
    }

    void tick_ease() {
        if (anim_t0_ < 0) return;
        double t = now_ms();
        float target = expanded ? 1.f : 0.f;
        float k = (float)((t - anim_t0_) / 200.0);
        if (k >= 1.f) { k = 1.f; anim_t0_ = -1; }
        ease_ = anim_from_ + (target - anim_from_) * k;
        if (host) host->mark_animating();
    }

    Size measure(Painter& p, Size avail) override {
        float inner_w = (std::isnan(hint_w) ? avail.w : hint_w) - padding * 2.f;
        float body = 0.f;
        for (size_t i = 0; i < children.size(); ++i) {
            Size cs = children[i]->measure(p, Size(inner_w, 1e6f));
            body += std::isnan(children[i]->hint_h) ? cs.h : children[i]->hint_h;
            if (i + 1 < children.size()) body += spacing;
        }
        body_h_ = body;
        float head = header_h + (subtitle.empty() ? 0.f : 2.f);
        return Size(or_default(hint_w, std::max(inner_w + padding * 2.f, 120.f)),
                    or_default(hint_h, head + ease_ * (body + (body > 0.f ? spacing : 0.f))));
    }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        if (children.empty()) return;
        Rect inner(r.x + padding, r.y + header_h + (subtitle.empty() ? 0.f : 2.f) + spacing,
                   r.w - padding * 2.f, std::max(0.f, body_h_));
        for (auto& c : children) {
            float h = std::isnan(c->hint_h) ? c->measure(p, Size(inner.w, inner.h)).h : c->hint_h;
            c->arrange(p, Rect(inner.x, inner.y, inner.w, h));
            inner.y += h + spacing;
        }
    }

    Widget* hit_test(Point pt) override {
        if (!bounds.contains(pt)) return nullptr;
        if (pt.y < bounds.y + header_h) return this;
        if (ease_ > 0.05f) {
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                if (Widget* w = (*it)->hit_test(pt)) return w;
        }
        return nullptr;
    }

    void paint(Painter& p) override {
        tick_anim();
        tick_ease();
        const Palette& pal = p.palette();
        float cy = bounds.y + header_h * 0.5f;
        Rect head(bounds.x, bounds.y, bounds.w, header_h);
        if (hovered) p.fill_round(head, Radius::control,
                                  pal.dark ? Color(1,1,1,0.04f) : Color(0,0,0,0.03f));
        p.glyph(expanded || ease_ > 0.5f ? glyph_open : glyph_close,
                Rect(bounds.x + 6.f, cy - 8.f, 16.f, 16.f), 11.f, pal.text_secondary);
        p.text(title, Rect(bounds.x + 28.f, bounds.y, bounds.w - 34.f, header_h), TextScale::Body,
               pal.text, HAlign::Start, VAlign::Middle, false);
        if (!subtitle.empty()) {
            float tw = p.measure_text(title, TextScale::Body).w;
            p.text(subtitle, Rect(bounds.x + 28.f + tw + 8.f, bounds.y,
                                  bounds.w - 40.f - tw, header_h), TextScale::Caption,
                   pal.text_tertiary, HAlign::Start, VAlign::Middle, false);
        }
        if (ease_ <= 0.02f) return;
        p.push_clip(Rect(bounds.x, bounds.y + header_h, bounds.w, bounds.h - header_h));
        p.push_translate(0.f, 0.f);
        for (auto& c : children) c->paint(p);
        p.pop_translate();
        p.pop_clip();
    }

    void on_click() override { set_expanded(!expanded); }

    float  ease_ = 0.f;
    float  body_h_ = 0.f;
    double anim_t0_ = -1.0;
    float  anim_from_ = 0.f;
};

// ---------------------------------------------------------- SelectorBar ----
// 分段选择条：一排等高的"选项",选中那个带浅底和一条底边强调线。
// 项文字/图标都由调用方给，库内不带任何预设项。
class SelectorBar : public Widget {
public:
    struct Item { std::wstring text; std::wstring glyph; };
    std::vector<Item> items;
    int selected = 0;
    float pad_x = 14.f;
    float min_item_w = 0.f;
    std::function<void(int)> on_select;

    template <class... Args>
    explicit SelectorBar(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        if (!o.items.empty()) {
            items.clear();
            for (auto& s : o.items) items.push_back(Item{s, L""});
        }
        selected = o.selected;
        change_name = o.change;
        click_name = o.click;
        hint_h = o.height;
        hint_w = o.width;
        enabled = o.enabled;
        min_item_w = o.min_width;
        pad_x = or_default(o.gap, 14.f);
    }

    int count() const { return (int)items.size(); }
    void set_selected(int i, bool notify = true) {
        if (items.empty()) return;
        if (i < 0) i = 0;
        if (i >= count()) i = count() - 1;
        if (i == selected) return;
        selected = i;
        if (host) host->request_repaint();
        if (notify) {
            if (on_select) on_select(selected);
            if (host && !change_name.empty()) host->fire(change_name, this, true);
        }
    }

    std::vector<float> widths(Painter& p) const {
        std::vector<float> w;
        for (auto& it : items) {
            float cw = p.measure_text(it.text, TextScale::Body).w + pad_x * 2.f;
            if (!it.glyph.empty()) cw += 22.f;
            w.push_back(std::max(cw, min_item_w));
        }
        return w;
    }

    Size measure(Painter& p, Size avail) override {
        (void)avail;
        auto w = widths(p);
        float total = 0.f;
        for (float v : w) total += v;
        return Size(or_default(hint_w, total), or_default(hint_h, 40.f));
    }

    void paint(Painter& p) override {
        tick_anim();
        const Palette& pal = p.palette();
        auto w = widths(p);
        cached_w_ = w;
        p.fill_round(bounds, Radius::control, pal.control.alpha(pal.dark ? 0.24f : 0.42f));
        float x = bounds.x;
        for (int i = 0; i < count(); ++i) {
            Rect r(x, bounds.y, w[(size_t)i], bounds.h);
            bool sel = (i == selected);
            if (sel) {
                p.fill_round(r.inset(2.f), Radius::control,
                             pal.dark ? Color(1,1,1,0.08f) : Color(1,1,1,0.85f));
                Rect line(r.x + r.w * 0.22f, r.bottom() - 2.5f, r.w * 0.56f, 2.5f);
                p.fill_round(line, 1.25f, pal.accent);
            } else if (hover_i_ == i) {
                p.fill_round(r.inset(2.f), Radius::control,
                             pal.dark ? Color(1,1,1,0.05f) : Color(0,0,0,0.04f));
            }
            float tx = r.x + pad_x;
            Color tc = sel ? pal.text : pal.text_secondary;
            if (!enabled) tc = pal.text_tertiary;
            if (!items[(size_t)i].glyph.empty()) {
                p.glyph(items[(size_t)i].glyph, Rect(tx, r.y + (r.h - 16.f) * 0.5f, 16.f, 16.f),
                        13.f, sel ? pal.accent : pal.text_secondary);
                tx += 22.f;
            }
            p.text(items[(size_t)i].text, Rect(tx, r.y, w[(size_t)i] - pad_x - (tx - r.x),
                                               r.h), TextScale::Body, tc,
                   HAlign::Start, VAlign::Middle, sel);
            x += w[(size_t)i];
        }
    }

    int index_at(Point pt) const {
        // 用固定估算避免再要 painter：按当前累计宽度（paint 时缓存）
        float x = bounds.x;
        for (int i = 0; i < count(); ++i) {
            float w = cached_w_.size() == items.size() ? cached_w_[(size_t)i] : 0.f;
            if (w <= 0.f) return -1;
            if (pt.x >= x && pt.x < x + w) return i;
            x += w;
        }
        return -1;
    }

    void on_click_at(Point pt) override {
        int i = index_at(pt);
        if (i >= 0) {
            set_selected(i);
            if (!click_name.empty() && host) host->fire(click_name, this, true);
        }
    }
    void on_move(Point pt) override {
        int i = index_at(pt);
        if (i != hover_i_) { hover_i_ = i; if (host) host->request_repaint(); }
    }
    void on_hover(bool on) override {
        Widget::on_hover(on);
        if (!on) { hover_i_ = -1; if (host) host->request_repaint(); }
    }
    bool interactive() const override { return enabled; }

    std::vector<float> cached_w_;
    int hover_i_ = -1;
};

// ------------------------------------------------------------ PipsPager ----
// 分页圆点：显示共几页、当前第几页，两侧可选的上一页/下一页箭头。
class PipsPager : public Widget {
public:
    int page_count = 5;
    int selected = 0;
    float pip = 8.f;
    float gap = 6.f;
    bool  arrows = false;
    std::wstring glyph_prev = L"\uE76B";   // ChevronLeft
    std::wstring glyph_next = L"\uE76C";   // ChevronRight
    std::function<void(int)> on_change;

    template <class... Args>
    explicit PipsPager(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        page_count = o.rating_max > 0 ? o.rating_max : 5;
        selected = o.rating;
        arrows = o.arrows;
        change_name = o.change;
        hint_h = o.height;
        enabled = o.enabled;
        if (o.width > 0.f) pip = o.width;
    }

    void set_selected(int i, bool notify = true) {
        if (page_count < 1) return;
        if (i < 0) i = 0;
        if (i >= page_count) i = page_count - 1;
        if (i == selected) return;
        selected = i;
        if (host) host->request_repaint();
        if (notify) {
            if (on_change) on_change(selected);
            if (host && !change_name.empty()) host->fire(change_name, this, true);
        }
    }

    float arrow_w() const { return arrows ? 26.f : 0.f; }

    Size measure(Painter& p, Size avail) override {
        (void)p; (void)avail;
        float w = (pip + gap) * (float)page_count - gap + arrow_w() * 2.f;
        return Size(or_default(hint_w, w), or_default(hint_h, 26.f));
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        float cy = bounds.y + bounds.h * 0.5f;
        float x = bounds.x + arrow_w();
        for (int i = 0; i < page_count; ++i) {
            Rect r(x, cy - pip * 0.5f, pip, pip);
            Color c = (i == selected) ? (enabled ? pal.accent : pal.text_tertiary)
                                      : pal.text_tertiary.alpha(0.55f);
            p.fill_circle(r.center(), pip * 0.5f, c);
            x += pip + gap;
        }
        if (arrows) {
            Color c = enabled ? pal.text_secondary : pal.text_tertiary;
            p.glyph(glyph_prev, Rect(bounds.x, cy - 9.f, 18.f, 18.f), 11.f, c);
            p.glyph(glyph_next, Rect(bounds.right() - 18.f, cy - 9.f, 18.f, 18.f), 11.f, c);
        }
    }

    void on_click_at(Point pt) override {
        if (arrows) {
            if (pt.x < bounds.x + arrow_w()) { set_selected(selected - 1); return; }
            if (pt.x > bounds.right() - arrow_w()) { set_selected(selected + 1); return; }
        }
        float rel = pt.x - bounds.x - arrow_w();
        int i = (int)std::floor((rel + gap * 0.5f) / (pip + gap));
        set_selected(i);
    }
    bool interactive() const override { return enabled; }
};

// --------------------------------------------------------- BreadcrumbBar ----
// 面包屑：一串路径片段，中间夹分隔字形，点哪段派发它的下标。
// 宽度不够时从左边开始折叠成省略号（只保证最后几段可见）。
class BreadcrumbBar : public Widget {
public:
    std::vector<std::wstring> items;
    std::wstring sep = L"\uE76C";
    float sep_w = 22.f;
    float ellipsis_w = 24.f;
    std::function<void(int)> on_pick;

    template <class... Args>
    explicit BreadcrumbBar(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        items = o.items;
        click_name = o.click;
        hint_h = o.height;
        hint_w = o.width;
        enabled = o.enabled;
    }

    void set_items(const std::vector<std::wstring>& v) {
        items = v;
        if (host) host->request_repaint();
    }

    Size measure(Painter& p, Size avail) override {
        (void)avail;
        float w = 0.f;
        for (size_t i = 0; i < items.size(); ++i) {
            w += p.measure_text(items[i], TextScale::Body).w;
            if (i + 1 < items.size()) w += sep_w;
        }
        return Size(or_default(hint_w, w + 4.f), or_default(hint_h, 32.f));
    }

    // 每段的宽度与起点，按 bounds 计算（窄了就折叠头部）
    void layout_pieces(Painter& p, std::vector<float>& w, std::vector<float>& x,
                       int& first) const {
        w.clear(); x.clear();
        float total = 0.f;
        for (size_t i = 0; i < items.size(); ++i) {
            float tw = p.measure_text(items[i], TextScale::Body).w;
            w.push_back(tw);
            total += tw + (i + 1 < items.size() ? sep_w : 0.f);
        }
        first = 0;
        if (total > bounds.w && items.size() > 2) {
            float drop = total - bounds.w + ellipsis_w;
            float acc = 0.f;
            for (size_t i = 0; i < items.size() - 2; ++i) {
                acc += w[i] + sep_w;
                first = (int)i + 1;
                if (acc >= drop) break;
            }
        }
        float xx = bounds.x + (first > 0 ? ellipsis_w : 0.f);
        for (size_t i = 0; i < items.size(); ++i) {
            if ((int)i < first) { x.push_back(-1.f); continue; }
            x.push_back(xx);
            xx += w[i] + sep_w;
        }
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        std::vector<float> w, x;
        int first = 0;
        layout_pieces(p, w, x, first);
        cached_w_ = w;
        cached_x_ = x;
        cached_first_ = first;
        float cy = bounds.y + bounds.h * 0.5f;
        if (first > 0) {
            p.text(L"\u2026", Rect(bounds.x, bounds.y, ellipsis_w, bounds.h), TextScale::Body,
                   pal.text_tertiary, HAlign::Start, VAlign::Middle, false);
            p.glyph(sep, Rect(bounds.x + ellipsis_w - 6.f, cy - 7.f, 14.f, 14.f), 9.f,
                    pal.text_tertiary);
        }
        for (size_t i = 0; i < items.size(); ++i) {
            if ((int)i < first) continue;
            bool last = (i + 1 == items.size());
            Color c = last ? pal.text : pal.text_secondary;
            if (!enabled) c = pal.text_tertiary;
            if (hover_i_ == (int)i && !last && enabled)
                p.fill_round(Rect(x[i] - 4.f, bounds.y + 4.f, w[i] + 8.f, bounds.h - 8.f),
                             Radius::control, pal.control.alpha(pal.dark ? 0.2f : 0.35f));
            p.text(items[i], Rect(x[i], bounds.y, w[i], bounds.h), TextScale::Body, c,
                   HAlign::Start, VAlign::Middle, last);
            if (!last)
                p.glyph(sep, Rect(x[i] + w[i] + (sep_w - 14.f) * 0.5f, cy - 7.f, 14.f, 14.f),
                        9.f, pal.text_tertiary);
        }
    }

    // 命中哪一段（用上一次 paint 缓存的几何，避免为了量字再要 Painter）
    int hit_at(Point pt) const {
        if (cached_w_.empty() || cached_x_.size() != cached_w_.size()) return -1;
        for (size_t i = 0; i < cached_w_.size(); ++i) {
            if ((int)i < cached_first_) continue;
            if (pt.x >= cached_x_[i] - 4.f && pt.x <= cached_x_[i] + cached_w_[i] + 4.f)
                return (int)i;
        }
        return -1;
    }

    void on_click_at(Point pt) override {
        int i = hit_at(pt);
        if (i < 0) return;
        if (on_pick) on_pick(i);
        if (!click_name.empty() && host) host->fire(click_name, this, true);
    }
    void on_move(Point pt) override {
        int i = hit_at(pt);
        if (i != hover_i_) { hover_i_ = i; if (host) host->request_repaint(); }
    }
    void on_hover(bool on) override {
        Widget::on_hover(on);
        if (!on) { hover_i_ = -1; if (host) host->request_repaint(); }
    }
    bool interactive() const override { return enabled; }

    int hover_i_ = -1;
    std::vector<float> cached_w_, cached_x_;
    int cached_first_ = 0;
};
// 下拉框 / 菜单展开后的列表面板：自己画阴影 / 圆角 / 行高亮 / 选中勾 / 分隔线 / 快捷键。
// 宿主窗口（Window）只负责「放在屏幕哪个位置」和「按方向展开的动画」。
class PopupPanel : public Widget {
public:
    std::vector<std::wstring> items;   // 每项可以是 "文本"、"文本\t快捷键"、"-"（分隔线）
    int   selected = -1;
    int   hover    = -1;
    float row_h    = 36.f;
    float radius   = Radius::flyout;
    float pad      = 4.f;      // 上下内边距
    float pad_x    = 8.f;      // 左右内边距（行在此基础上再内缩）
    float text_inset = 20.f;   // 文字离行左边缘的距离（Win11 菜单留得比按钮更宽）
    bool  show_check = true;   // 选中项左边打勾（菜单模式下不打勾）

    std::function<void(int)> on_pick;     // 选中某行
    std::function<void(int)> on_submenu;  // 某行有下一级：请求拉出子菜单
    std::function<void()>    on_submenu_leave;  // 离开了带 ▸ 的那一行：收掉下一级
    std::function<bool()>    child_open;        // 由 Window 挂：这一层下面是不是还弹着子菜单
    std::function<void()>    on_cancel;   // 关闭动画结束（或点空白关闭）
    std::function<void()>    on_dirty;    // 需要重画（hover 变化）

    // 这两项由 Window 在弹层创建/收掉时写，面板自己不去猜：
    // popup_level   —— 这一层是第几层（0 = 最外层），子菜单要开在它右边，而不是"最深一层"右边；
    // open_child_row —— 此刻这一层已经开着的子菜单是从哪一行拉出来的（点同一行不再重复开）。
    int popup_level = -1;
    int open_child_row = -1;

    // 这几行右侧画 ▸（菜单里的"还有一层"），点它们不选值、而是继续往右展开
    std::vector<int> submenu_rows;
    bool has_submenu(int i) const {
        for (int r : submenu_rows) if (r == i) return true;
        return false;
    }

    float shadow_blur() const override { return 4.5f; }
    float shadow_offset() const override { return 2.5f; }
    float shadow_radius() const override { return radius; }

    // 面板内部还有自己的动画时为真（日历翻月的滚动、取色器拖动之类）。
    // PopupWin 靠它决定"弹层展开动画都结束了还要不要继续出帧"。
    virtual bool animating() const { return false; }

    // ---- 局部动画：只合成一小块，别整块重建 ----
    // 整块重建要重画 16 层阴影 + 重排所有文字，实测一帧 22~42 ms，动画必然"卡卡的"。
    // motion_area 声明"这块像素会自己变"，PopupWin 每次整块重建后都把这块留一张，
    // 之后面板 animating() 期间每帧在 CPU 里把"切之前 / 切之后"两张按进度拼起来 —— 不进 D2D。
    // 默认不参与：motion_area 返回 false 时一切照旧。
    virtual bool  motion_area(Rect& r) const { (void)r; return false; }
    virtual float motion_e() const { return 1.f; }
    virtual int   motion_dir() const { return 1; }

    // ---- 子菜单：悬浮自动展开 ----
    // Win11 / Typedown 的菜单都是"鼠标停在这一行上一会儿，子菜单自己出来"，
    // 不需要点。click 只在"要立刻展开"的场合才用。
    bool   hover_open = true;         // 是否悬浮自动展开
    double hover_delay_ms = 0.0;      // 悬停多久之后展开（0 = 停上去立刻出）
    int    hover_open_mode = -1;      // -1=跟随弹层默认设置, 0=强制关, 1=强制开
    int    hover_sub_row = -1;        // 当前停住的、带子菜单的那一行
    double hover_sub_t = 0.0;         // 停住的时间点
    bool   hover_sub_armed = false;   // 已经展开过（避免每帧重复展开）

    static bool is_separator(const std::wstring& s) { return s == L"-" || s == L"\u2500"; }

    // 分隔线只占很薄的一条，不占满一整行（Win11 的菜单就是这样）
    static constexpr float sep_h = 10.f;
    static float item_height(const std::wstring& s, float rh) { return is_separator(s) ? sep_h : rh; }
    static float content_height(const std::vector<std::wstring>& v, float rh, float padv) {
        float t = padv * 2.f;
        for (auto& s : v) t += item_height(s, rh);
        return t;
    }
    // 在 avail 高度里能完整放下的高度（按整项对齐）
    static float fit_height(const std::vector<std::wstring>& v, float rh, float padv, float avail) {
        float used = padv;
        for (auto& s : v) {
            float ih = item_height(s, rh);
            if (used + ih + padv > avail + 0.01f) break;
            used += ih;
        }
        return used + padv;
    }

    void arrange(Painter& p, const Rect& r) override {
        (void)p;
        bounds = r;
    }

    Rect row_rect(int i) const {
        float y = bounds.y + pad;
        for (int k = 0; k < i; ++k) y += item_height(items[(size_t)k], row_h);
        return Rect(bounds.x + pad_x, y, bounds.w - pad_x * 2.f, item_height(items[(size_t)i], row_h));
    }

    // 行文字与快捷键（用 \t 分隔）
    static void split_item(const std::wstring& s, std::wstring& label, std::wstring& accel) {
        size_t t = s.find(L'\t');
        if (t == std::wstring::npos) {
            label = s;
            accel.clear();
        } else {
            label = s.substr(0, t);
            accel = s.substr(t + 1);
        }
    }

    // 鼠标点落在第几行：直接拿画出来的行矩形做命中，避免"检测区整体偏移"这种问题
    int row_at(Point pt) const {
        if (!bounds.contains(pt)) return -1;
        for (int i = 0; i < (int)items.size(); ++i) {
            if (is_separator(items[(size_t)i])) continue;
            Rect r = row_rect(i);
            if (r.bottom() > bounds.bottom() - pad + 0.5f) continue;   // 被面板高度裁掉的行
            if (pt.x >= r.x && pt.x <= r.right() && pt.y >= r.y && pt.y <= r.bottom()) return i;
        }
        return -1;
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        draw_shadow(p);
        const Color face = pal.flyout;
        p.fill_round(bounds, radius, face);
        // 浮层描边用 flyout_border（深色），不用控件那套浅描边：
        // 父子面板贴在一起时，只有深色 1px 边才能画出 Edge 那种"看得出边界"的分界
        p.stroke_round(bounds.inset(0.5f), radius, pal.flyout_border, 1.f);

        for (int i = 0; i < (int)items.size(); ++i) {
            Rect r = row_rect(i);
            if (r.bottom() > bounds.bottom() - pad + 0.01f) break;

            if (is_separator(items[(size_t)i])) {
                // 一条细的分隔线：2px + 高浓度，隔着桌子也能看见，但仍只是一根线
                float y = r.y + r.h * 0.5f;
                p.fill_rect(Rect(r.x + 10.f, y - 0.5f, r.w - 20.f, 2.f), pal.flyout_sep);
                continue;
            }

            Color rowfill(0.f, 0.f, 0.f, 0.f);
            if (i == hover) rowfill = pal.flyout_hover;   // 选中项不铺蓝底，只打勾
            if (rowfill.a > 0.001f) p.fill_round(r, Radius::control, rowfill);

            std::wstring label, accel;
            split_item(items[(size_t)i], label, accel);

            float right_pad = 12.f;
            float accel_w = accel.empty() ? 0.f : p.measure_text(accel, TextScale::Body).w;
            bool sub = has_submenu(i);
            if (sub) right_pad += 16.f;   // 给 ▸ 让位
            if (!accel.empty()) {
                p.text(accel, Rect(r.right() - right_pad - accel_w, r.y, accel_w, r.h),
                       TextScale::Body, pal.text_secondary, HAlign::End, VAlign::Middle, false);
                right_pad += accel_w + 12.f;
            }
            // 勾在文字左边（Win11 菜单的习惯）
            float text_x = r.x + text_inset;
            if (show_check) {
                if (i == selected)
                    p.glyph(L"\uE73E", Rect(text_x - 6.f, r.y, 16.f, r.h), 11.f, pal.accent);
                text_x += 20.f;
            }
            float text_w = r.right() - right_pad - text_x;
            p.text(label, Rect(text_x, r.y, text_w, r.h), TextScale::Body, pal.text,
                   HAlign::Start, VAlign::Middle, false);
            if (sub) {
                // Win11 菜单的"还有一层"箭头
                p.glyph(L"\uE76C", Rect(r.right() - 22.f, r.y, 14.f, r.h), 10.f, pal.text_secondary);
            }
        }
    }

    void mark_dirty() {
        if (on_dirty) on_dirty();
        else if (host) host->request_repaint();
    }
    // 鼠标按下：默认什么都不做，取色器靠它起"按住拖"（HSV 方块 / 色相条）
    virtual void mouse_down(Point pt) { (void)pt; }
    virtual void mouse_move(Point pt) {
        ++mm_counter();
        int i = row_at(pt);
        if (i != hover) {
            hover = i;
            if (hover_open && i >= 0 && has_submenu(i)) {
                // 停到带 ▸ 的行上：开始计时，到点就展开；换行则重新计时
                hover_sub_row = i;
                hover_sub_t = now_ms();
                hover_sub_armed = false;
            } else if (hover_sub_row != -1 || (child_open && child_open())) {
                // 离开那一行：这一层的子菜单要收掉（下一层以下的也一起收）
                hover_sub_row = -1;
                hover_sub_armed = false;
                if (on_submenu_leave) on_submenu_leave();
            }
            mark_dirty();
        }
    }
    virtual void mouse_leave() {
        if (hover != -1) {
            hover = -1;
            if (hover_sub_row != -1 || (child_open && child_open())) {
                hover_sub_row = -1;
                hover_sub_armed = false;
                if (on_submenu_leave) on_submenu_leave();
            }
            mark_dirty();
        }
    }
    // 还差多少毫秒到点（弹层用它决定要不要起计时器）
    bool hover_waiting() const { return hover_open && hover_sub_row >= 0 && !hover_sub_armed; }
    // 到点了就该展开这一行（每停一次只返回一次）
    int tick_hover(double now) {
        if (!hover_waiting()) return -1;
        if (now - hover_sub_t < hover_delay_ms) return -1;
        hover_sub_armed = true;
        return hover_sub_row;
    }
    virtual void click(Point pt) {
        int i = row_at(pt);
        if (i < 0) return;
        if (has_submenu(i) && on_submenu) {
            // 这一行的子菜单已经开着了（多半是刚才悬浮自动展开的）：
            // 再点一次不能再开一遍，否则会在最深处不断往上堆同样的面板。
            if (open_child_row == i && child_open && child_open()) return;
            if (host) host->note_submenu_source(popup_level);   // 子菜单要开在"我这一层"右边
            on_submenu(i);
            return;
        }
        if (on_pick) on_pick(i);
    }
};

// 小组件（取色器 / 日期选择器 / 月历 / 数字小键盘）都在 mnicompt.hpp：
// 它们属于"大一号"的组合控件，不跟基础组件一起膨胀。包含 <w11ui/w11ui.hpp>
// 或单独包含 <w11ui/mnicompt.hpp> 都能拿到。

// 右键菜单：任何挂了 kw::context() + kw::items({...}) 的控件都能弹。
// 项的行高/圆角/阴影和下拉框一致；点中某项就派发 change_name，并调 on_context_pick(i)。
// ------------------------------------------------------- TimePicker ----
// 时间面板：时 / 分 /（秒）三列滚轮，程序唤起、程序给格式、程序拿结果。
// 每列显示 rows 行（默认 5），中间那行就是当前值；点某行或按箭头步进。
class TimePicker : public PopupPanel {
public:
    int hour = 0, minute = 0, second = 0;
    bool  show_seconds = false;
    int   hour_step = 1, minute_step = 1, second_step = 1;
    int   hour_max = 23, minute_max = 59, second_max = 59;
    std::wstring header_text;      // 面板顶部文字（空 = 不画）
    std::wstring confirm_text;     // 右下角按钮（空 = 点即生效、面板自动关）
    std::wstring time_format = L"HH:mm";   // 程序写回输入框时用的格式
    int   rows = 5;
    float pad = 12.f, row_h = 34.f, col_w = 74.f, head_h = 36.f, foot_h = 36.f;

    std::function<void(int, int, int)> on_pick_time;

    static int clamp_i_(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static std::wstring two_digits_(int v) {
        std::wstring s;
        if (v < 10) s += L'0';
        s += std::to_wstring(v);
        return s;
    }

    int cols() const { return show_seconds ? 3 : 2; }
    float preferred_width() const {
        return pad * 2.f + col_w * (float)cols() + 8.f * (float)(cols() - 1);
    }
    float preferred_height() const {
        float h = pad * 2.f + row_h * (float)rows;
        if (!header_text.empty()) h += head_h;
        if (!confirm_text.empty()) h += 8.f + 1.f + foot_h;
        return h;
    }

    TimePicker() {
        cast_shadow = true;
        radius = Radius::flyout;
    }
    template <class... Args>
    explicit TimePicker(Args&&... args) : TimePicker() {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        hour = clamp_i_(o.hour, 0, hour_max);
        minute = clamp_i_(o.minute, 0, minute_max);
        second = clamp_i_(o.second, 0, second_max);
        show_seconds = o.show_seconds;
        header_text = o.title;
        confirm_text = o.ok_text;
        if (!o.time_format.empty()) time_format = o.time_format;
        on_pick_time = o.on_pick_time;
        if (!std::isnan(o.width) && o.width > 80.f) col_w = (o.width - pad * 2.f - 8.f) / (float)cols();
    }

    void set_time(int h, int m, int s = 0) {
        hour = clamp_i_(h, 0, hour_max);
        minute = clamp_i_(m, 0, minute_max);
        second = clamp_i_(s, 0, second_max);
        mark_dirty();
    }

    void arrange(Painter& p, const Rect& r) override {
        (void)p;
        bounds = r;
        float y = r.y + pad;
        if (!header_text.empty()) y += head_h;
        grid_y_ = y;
        float x = r.x + pad;
        for (int c = 0; c < 3; ++c) {
            col_x_[c] = x;
            up_r_[c]   = Rect(x, grid_y_ - 2.f, col_w, 2.f);   // 箭头不在 paint 里画，靠点击行
            x += col_w + 8.f;
        }
        foot_y_ = r.y + r.h - pad - foot_h;
    }

    // 第 c 列第 i 行显示的值（环形，越界回绕）
    int value_at(int c, int i) const {
        int maxv = (c == 0 ? hour_max : (c == 1 ? minute_max : second_max));
        int step = (c == 0 ? hour_step : (c == 1 ? minute_step : second_step));
        int base = (c == 0 ? hour : (c == 1 ? minute : second));
        int mid = rows / 2;
        int v = base + (i - mid) * step;
        int span = maxv + 1;
        int sv = step > 0 ? step : 1;
        // 按 step 回绕：把 v 规到 [0, maxv]
        while (v < 0) v += span;
        while (v > maxv) v -= span;
        (void)sv;
        return v;
    }

    Rect row_rect(int c, int i) const {
        return Rect(col_x_[c], grid_y_ + row_h * (float)i, col_w, row_h);
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        draw_shadow(p);
        p.fill_round(bounds, radius, pal.flyout);
        p.stroke_round(bounds.inset(0.5f), radius, pal.flyout_border, 1.f);
        if (!header_text.empty())
            p.text(header_text, Rect(bounds.x + pad, bounds.y + pad * 0.5f,
                                     bounds.w - pad * 2.f, head_h), TextScale::BodyStrong,
                   pal.text, HAlign::Start, VAlign::Middle, true);
        int mid = rows / 2;
        for (int c = 0; c < cols(); ++c) {
            // 中间行的高亮带（Win11 那圈浅底）
            Rect hi = row_rect(c, mid);
            p.fill_round(hi.inset(2.f), Radius::control,
                         pal.dark ? Color(1, 1, 1, 0.07f) : Color(0, 0, 0, 0.045f));
            for (int i = 0; i < rows; ++i) {
                Rect r = row_rect(c, i);
                float dist = std::abs((float)(i - mid));
                Color tc = (i == mid) ? pal.text
                                      : (dist > 2.f ? pal.text_tertiary : pal.text_secondary);
                if (i == mid) tc = pal.text;
                std::wstring s = two_digits_(value_at(c, i));
                p.text(s, r, (i == mid ? TextScale::BodyStrong : TextScale::Body), tc,
                       HAlign::Center, VAlign::Middle, i == mid);
            }
            // 列分隔线
            if (c + 1 < cols()) {
                Rect sep(col_x_[c] + col_w + 3.5f, grid_y_ + 8.f, 1.f, row_h * (float)rows - 16.f);
                p.fill_rect(sep, pal.control_stroke.alpha(0.35f));
            }
        }
        if (!confirm_text.empty()) {
            Rect br(bounds.right() - pad - 76.f, foot_y_, 76.f, foot_h);
            p.fill_round(br, Radius::control, pal.accent);
            p.text(confirm_text, br, TextScale::Body, Color(1,1,1,1), HAlign::Center,
                   VAlign::Middle, false);
        }
    }

    void on_click_at(Point pt) override {
        if (!confirm_text.empty() && pt.y >= foot_y_ && pt.x > bounds.right() - pad - 90.f) {
            commit();
            return;
        }
        int mid = rows / 2;
        for (int c = 0; c < cols(); ++c) {
            for (int i = 0; i < rows; ++i) {
                if (!row_rect(c, i).contains(pt)) continue;
                int v = value_at(c, i);
                if (c == 0) hour = v; else if (c == 1) minute = v; else second = v;
                mark_dirty();
                if (confirm_text.empty()) commit();
                return;
            }
        }
    }

    void commit() {
        if (on_pick_time) on_pick_time(hour, minute, second);
        if (host) {
            if (!change_name.empty()) host->fire(change_name, this, true);
            host->close_popup();
        }
    }

    Rect up_r_[3]{};
    float col_x_[3]{};
    float grid_y_ = 0.f, foot_y_ = 0.f;
};

// ----------------------------------------------------- AutoSuggestBox ----
// 带候选下拉的搜索框：输入时问程序要候选（on_query），下拉里点一项或回车取一项。
// 候选内容一条都不在库里，程序不给 items 就不弹。
class AutoSuggestBox : public TextBox {
public:
    std::vector<std::wstring> items;
    std::function<void(const std::wstring&)> on_query;
    std::function<void(int, const std::wstring&)> on_pick;
    std::wstring glyph_search = L"\uE721";   // 放大镜
    std::wstring glyph_clear  = L"\uE711";   // 清空
    bool  show_search_icon = true;
    float icon_w = 28.f;
    std::shared_ptr<PopupPanel> panel;
    bool opened = false;
    int  pick = -1;

    template <class... Args>
    explicit AutoSuggestBox(Args&&... args) : TextBox(std::forward<Args>(args)...) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        items = o.items;
        if (!o.placeholder.empty()) placeholder = o.placeholder;
        if (!o.change.empty()) change_name = o.change;
    }

    void set_items(const std::vector<std::wstring>& v) { items = v; }

    Rect content_rect() const override {
        Rect r = bounds;
        if (show_search_icon) { r.x += icon_w; r.w -= icon_w; }
        r.w -= 26.f;   // 右侧清空钮
        return r;
    }

    void paint(Painter& p) override {
        TextBox::paint(p);
        const Palette& pal = p.palette();
        float cy = bounds.y + bounds.h * 0.5f;
        if (show_search_icon)
            p.glyph(glyph_search, Rect(bounds.x + 8.f, cy - 8.f, 16.f, 16.f), 12.f,
                    pal.text_tertiary);
        if (!text.empty())
            p.glyph(glyph_clear, Rect(bounds.right() - 24.f, cy - 7.f, 14.f, 14.f), 10.f,
                    pal.text_secondary);
    }

    void on_click_at(Point pt) override {
        if (!text.empty() && pt.x > bounds.right() - 26.f) {
            text.clear();
            caret = 0;
            anchor = 0;
            if (host) host->request_repaint();
            query();
            return;
        }
        TextBox::on_click_at(pt);
    }

    void on_char(wchar_t ch) override {
        TextBox::on_char(ch);
        query();
    }
    bool on_key(int vk, bool ctrl, bool shift) override {
        if (vk == VK_DOWN && opened && panel) {
            int n = (int)items.size();
            if (n > 0) { pick = (pick + 1) % n; panel->hover = pick; if (host) host->request_repaint(); }
            return true;
        }
        if (vk == VK_UP && opened && panel) {
            int n = (int)items.size();
            if (n > 0) { pick = (pick - 1 + n) % n; panel->hover = pick; if (host) host->request_repaint(); }
            return true;
        }
        if (vk == VK_RETURN && opened && pick >= 0 && pick < (int)items.size()) {
            accept(pick);
            return true;
        }
        if (vk == VK_ESCAPE && opened) { close(); return true; }
        return TextBox::on_key(vk, ctrl, shift);
    }

    void query() {
        if (on_query) on_query(text);
        if (items.empty()) { close(); return; }
        show_panel();
    }

    void accept(int i) {
        if (i < 0 || i >= (int)items.size()) return;
        pick = i;
        std::wstring v = items[(size_t)i];
        text = v;
        caret = (int)v.size();
        if (on_pick) on_pick(i, v);
        close();
        if (host) {
            if (!change_name.empty()) host->fire(change_name, this, true);
            host->request_repaint();
        }
    }

    void show_panel() {
        if (!host) return;
        float row_h = 34.f;
        float pw = std::max(bounds.w, 200.f);
        float want_h = PopupPanel::content_height(items, row_h, 4.f);
        Rect region = host->client_rect();
        float gap = 4.f;
        float below = region.bottom() - (bounds.bottom() + gap);
        float above = bounds.y - gap - region.y;
        PopupDir pd = PopupDir::Down;
        float h = want_h;
        if (below >= want_h) pd = PopupDir::Down;
        else if (above >= want_h) pd = PopupDir::Up;
        else { pd = (below >= above) ? PopupDir::Down : PopupDir::Up;
               h = std::max(row_h + 8.f, below >= above ? below : above); }

        auto self = std::static_pointer_cast<AutoSuggestBox>(shared_from_this());
        if (!panel) {
            panel = std::make_shared<PopupPanel>();
            panel->on_pick = [self](int i) { self->accept(i); };
            panel->on_cancel = [self]() { self->opened = false; self->pick = -1; };
        }
        panel->items = items;
        panel->hover = pick;
        panel->row_h = row_h;
        panel->radius = Radius::flyout;
        panel->cast_shadow = true;
        panel->show_check = false;
        Rect cs = host->client_screen_rect();
        float sc = host->dpi_scale();
        Rect r(cs.x + bounds.x * sc, cs.y + (bounds.y + bounds.h + gap) * sc, pw * sc, h * sc);
        if (pd == PopupDir::Up)
            r.y = cs.y + (bounds.y - gap - h) * sc;
        opened = true;
        host->show_popup(panel, r, pd);
    }

    void close() {
        if (!host) return;
        opened = false;
        pick = -1;
        host->close_popup();
    }

    void on_focus(bool on) override {
        TextBox::on_focus(on);
        if (!on) close();
    }
};

// --------------------------------------------------------- TeachingTip ----
// 教学提示卡：图标 + 标题 + 正文 + 一个主按钮 + 右上角关闭。程序唤起、程序给全部文案。
class TeachingTip : public PopupPanel {
public:
    std::wstring title;
    std::wstring body;
    std::wstring glyph = L"\uE946";       // 默认是信息图标
    std::wstring primary_text;            // 主按钮文字（空 = 不画）
    bool  closable = true;
    float card_w = 340.f;
    float pad = 16.f, head_h = 30.f, body_h = 0.f, foot_h = 34.f;

    std::function<void()> on_action;
    std::function<void()> on_dismiss;

    float preferred_width() const { return card_w; }
    float preferred_height() const {
        float h = pad * 2.f + head_h;
        if (!body.empty()) h += 6.f + (body_h > 0.f ? body_h : 40.f);
        if (!primary_text.empty()) h += 10.f + foot_h;
        return h;
    }

    TeachingTip() {
        cast_shadow = true;
        radius = Radius::flyout;
    }
    template <class... Args>
    explicit TeachingTip(Args&&... args) : TeachingTip() {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        title = o.title;
        body = o.text;
        if (!o.icon.empty()) glyph = o.icon;
        primary_text = o.ok_text;
        closable = true;                    // 右上角那个 × 一直在，程序可以设 false 关掉
        if (!std::isnan(o.width) && o.width > 160.f) card_w = o.width;
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        draw_shadow(p);
        p.fill_round(bounds, radius, pal.flyout);
        p.stroke_round(bounds.inset(0.5f), radius, pal.flyout_border, 1.f);

        float x = bounds.x + pad;
        float y = bounds.y + pad;
        p.glyph(glyph, Rect(x, y + (head_h - 18.f) * 0.5f, 18.f, 18.f), 15.f, pal.accent);
        float tw = bounds.w - pad * 2.f - 26.f;
        p.text(title, Rect(x + 26.f, y, tw, head_h), TextScale::BodyStrong, pal.text,
               HAlign::Start, VAlign::Middle, true);
        if (closable)
            p.glyph(L"\uE711", Rect(bounds.right() - pad - 12.f, y + (head_h - 12.f) * 0.5f,
                                     12.f, 12.f), 10.f, pal.text_secondary);

        if (!body.empty()) {
            Rect br(bounds.x + pad, y + head_h + 6.f, bounds.w - pad * 2.f,
                    (body_h > 0.f ? body_h : 40.f));
            p.text(body, br, TextScale::Caption, pal.text_secondary, HAlign::Start,
                   VAlign::Top, true);
        }
        if (!primary_text.empty()) {
            float bw = p.measure_text(primary_text, TextScale::Body).w + 28.f;
            Rect b(bounds.x + pad, bounds.bottom() - pad - foot_h, bw, foot_h);
            p.fill_round(b, Radius::control, pal.accent);
            p.text(primary_text, b, TextScale::Body, Color(1,1,1,1), HAlign::Center,
                   VAlign::Middle, false);
        }
    }

    void on_click_at(Point pt) override {
        float y = bounds.y + pad;
        if (closable && pt.y <= y + head_h && pt.x >= bounds.right() - pad - 18.f) {
            if (on_dismiss) on_dismiss();
            if (host) host->close_popup();
            return;
        }
        if (!primary_text.empty()) {
            Rect bo(bounds.x + pad, bounds.bottom() - pad - foot_h,
                    bounds.w - pad * 2.f, foot_h);
            if (bo.contains(pt) && pt.x < bo.x + 200.f) {
                if (on_action) on_action();
                if (host) host->close_popup();
            }
        }
    }
};


// ---------------------------------------------------------- NavigationView ----
// 左导航 + 右内容。导航项文字/图标全由程序给，右侧页面自己 add 进来。
class NavigationView : public Widget {
public:
    std::vector<std::wstring> items;
    std::vector<std::wstring> glyphs;         // 可选，跟 items 一一对应
    int  selected = 0;
    float pane_w = 220.f, item_h = 36.f, pad = 8.f, indent = 12.f;
    std::shared_ptr<Widget> content;
    std::function<void(int)> on_select;

    template <class... Args>
    explicit NavigationView(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        items = o.items;
        if (!o.icon.empty()) glyphs.push_back(o.icon);
        selected = o.selected < 0 ? 0 : o.selected;
        if (!std::isnan(o.width)) pane_w = o.width;
        if (!std::isnan(o.height)) item_h = o.height;
        hint_w = o.max_width;      // 整块宽由 max_width 给；height 已经当行高用了
        flex = o.flex;
    }

    void attach_content(std::shared_ptr<Widget> w) {
        content = std::move(w);
        if (content && host) content->attach(host);
    }
    void on_attach() override { if (content) content->attach(host); }

    Rect item_rect(int i) const {
        return Rect(bounds.x + pad, bounds.y + pad + item_h * (float)i + 4.f * (float)i,
                    pane_w - pad * 2.f, item_h);
    }

    Size measure(Painter& p, Size avail) override {
        (void)p; (void)avail;
        return Size(or_default(hint_w, 560.f), or_default(hint_h, 240.f));
    }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        if (content) {
            Rect cr(r.x + pane_w, r.y, std::max(0.f, r.w - pane_w), r.h);
            content->measure(p, Size(cr.w, cr.h));
            content->arrange(p, cr);
        }
    }

    Widget* hit_test(Point pt) override {
        if (!bounds.contains(pt)) return nullptr;
        if (pt.x >= bounds.x + pane_w && content) {
            if (Widget* w = content->hit_test(pt)) return w;
        }
        return this;
    }

    void on_click_at(Point pt) override {
        for (int i = 0; i < (int)items.size(); ++i) {
            if (item_rect(i).contains(pt)) {
                selected = i;
                if (on_select) on_select(i);
                if (host) { host->request_repaint(); if (!change_name.empty()) host->fire(change_name, this, true); }
                return;
            }
        }
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        if (content) content->paint(p);
        p.fill_rect(Rect(bounds.x, bounds.y, pane_w, bounds.h), pal.card);
        Rect line(bounds.x + pane_w - 1.f, bounds.y, 1.f, bounds.h);
        p.fill_rect(line, pal.control_stroke.alpha(0.5f));

        for (int i = 0; i < (int)items.size(); ++i) {
            Rect ir = item_rect(i);
            bool on = i == selected;
            if (on) {
                p.fill_round(ir, Radius::control, pal.accent.alpha(0.14f));
                p.fill_round(Rect(ir.x + 2.f, ir.y + ir.h * 0.28f, 3.f, ir.h * 0.44f), 1.5f, pal.accent);
            }
            float tx = ir.x + indent;
            if (!glyphs.empty() && i < (int)glyphs.size()) {
                p.glyph(glyphs[(size_t)i], Rect(tx, ir.y + (ir.h - 16.f) * 0.5f, 16.f, 16.f), 12.f,
                        on ? pal.accent : pal.text_secondary);
                tx += 24.f;
            }
            p.text(items[(size_t)i], Rect(tx, ir.y, ir.right() - tx - 8.f, ir.h),
                   TextScale::Body, on ? pal.text : pal.text_secondary, HAlign::Start,
                   VAlign::Middle, false);
        }
    }
};

// ------------------------------------------------------------ SplitView ----
// 左右分栏：左栏可以是"推挤式"（内容让位）或"覆盖式"（浮在上面 + 遮罩）。
// 左栏和右侧内容都交给程序填，库这边只负责几何与一条分界线。
class SplitView : public Widget {
public:
    std::shared_ptr<Widget> pane;
    std::vector<std::shared_ptr<Widget>> content;
    bool  open = true;
    bool  overlay = false;        // true = 左栏浮在内容上，带遮罩
    float pane_w = 260.f;
    float scrim_alpha = 0.35f;

    template <class... Args>
    explicit SplitView(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        open = o.checked;
        overlay = o.wrap;
        if (!std::isnan(o.width) && o.width > 80.f) pane_w = o.width;
        hint_h = o.height;
        flex = o.flex;
        enabled = o.enabled;
    }

    void set_open(bool on) {
        open = on;
        if (host) { host->request_repaint(); host->mark_animating(); }
        if (host && !change_name.empty()) host->fire(change_name, this, open);
    }

    void attach_pane(std::shared_ptr<Widget> w) {
        pane = std::move(w);
        if (pane && host) pane->attach(host);
    }
    std::shared_ptr<SplitView> add(std::shared_ptr<Widget> w) {
        if (!w) return this_();
        if (host) w->attach(host);
        content.push_back(std::move(w));
        if (host) host->request_repaint();
        return this_();
    }
    std::shared_ptr<SplitView> this_() {
        return std::static_pointer_cast<SplitView>(shared_from_this());
    }

    void on_attach() override {
        if (pane) pane->attach(host);
        for (auto& c : content) c->attach(host);
    }

    Size measure(Painter& p, Size avail) override {
        (void)p; (void)avail;
        return Size(or_default(hint_w, 600.f), or_default(hint_h, 320.f));
    }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        float pw = open ? pane_w : 0.f;
        if (overlay) {
            if (pane) {
                pane->measure(p, Size(pane_w, r.h));
                pane->arrange(p, Rect(r.x, r.y, pane_w, r.h));
            }
            for (auto& c : content) {
                c->measure(p, Size(r.w, r.h));
                c->arrange(p, r);
            }
        } else {
            Rect right(r.x + pw, r.y, std::max(0.f, r.w - pw), r.h);
            if (pane && open) {
                pane->measure(p, Size(pw, r.h));
                pane->arrange(p, Rect(r.x, r.y, pw, r.h));
            }
            for (auto& c : content) {
                c->measure(p, Size(right.w, right.h));
                c->arrange(p, right);
            }
        }
    }

    Widget* hit_test(Point pt) override {
        if (!bounds.contains(pt)) return nullptr;
        if (overlay && open) {
            if (pane) {
                if (Widget* w = pane->hit_test(pt)) return w;
            }
            if (pt.x < bounds.x + pane_w) return this;
            return this;   // 遮罩接住点击 = 收起
        }
        if (pane && open && pt.x < bounds.x + pane_w) {
            if (Widget* w = pane->hit_test(pt)) return w;
            return this;
        }
        for (auto it = content.rbegin(); it != content.rend(); ++it)
            if (Widget* w = (*it)->hit_test(pt)) return w;
        return this;
    }

    void on_click_at(Point) override {
        if (overlay && open) set_open(false);
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        p.push_clip(bounds);
        for (auto& c : content) c->paint(p);
        if (!open) { p.pop_clip(); return; }
        if (overlay) {
            // 遮罩盖住右侧，然后左栏压上来
            Rect right(bounds.x + pane_w, bounds.y, bounds.w - pane_w, bounds.h);
            p.fill_rect(right, Color(0.f, 0.f, 0.f, scrim_alpha));
        }
        if (pane) pane->paint(p);
        // 分界线（1px，浅色，深色下用亮一点的白）
        Rect line(bounds.x + pane_w - 1.f, bounds.y, 1.f, bounds.h);
        p.fill_rect(line, pal.control_stroke.alpha(0.5f));
        p.pop_clip();
    }
};

// -------------------------------------------------------------- FlipView ----
// 翻页容器：一次只显示一页，左右箭头或点下方指示切换。页内容由程序 add 进来。
class FlipView : public Widget {
public:
    std::vector<std::shared_ptr<Widget>> pages;
    int  index = 0;
    bool show_arrows = true;
    float arrow_w = 32.f;
    std::wstring glyph_prev = L"\uE76B";
    std::wstring glyph_next = L"\uE76C";
    std::function<void(int)> on_change;

    template <class... Args>
    explicit FlipView(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        index = o.selected < 0 ? 0 : o.selected;   // selected 默认 -1（跟 ComboBox 一致）
        show_arrows = !o.closable;    // closable 取反当"藏箭头"，避免又加一个选项
        if (!o.change.empty()) change_name = o.change;
        hint_h = o.height;
        hint_w = o.width;
        flex = o.flex;
    }

    std::shared_ptr<FlipView> add(std::shared_ptr<Widget> w) {
        if (!w) return std::static_pointer_cast<FlipView>(shared_from_this());
        if (host) w->attach(host);
        pages.push_back(std::move(w));
        if (host) host->request_repaint();
        return std::static_pointer_cast<FlipView>(shared_from_this());
    }
    void on_attach() override {
        for (auto& w : pages) w->attach(host);
    }

    void set_index(int i, bool notify = true) {
        if (pages.empty()) return;
        if (i < 0) i = 0;
        if (i >= (int)pages.size()) i = (int)pages.size() - 1;
        if (i == index) return;
        index = i;
        if (host) host->request_repaint();
        if (notify) {
            if (on_change) on_change(index);
            if (host && !change_name.empty()) host->fire(change_name, this, true);
        }
    }

    Size measure(Painter& p, Size avail) override {
        (void)p; (void)avail;
        return Size(or_default(hint_w, 400.f), or_default(hint_h, 240.f));
    }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        Rect body = body_rect();
        for (auto& w : pages) {
            if (!w) continue;
            w->measure(p, Size(body.w, body.h));   // 先量再排，免得子控件拿到 0 高度
            w->arrange(p, body);
        }
    }
    Rect body_rect() const {
        float l = show_arrows ? arrow_w : 0.f;
        return Rect(bounds.x + l, bounds.y, std::max(0.f, bounds.w - l * 2.f), bounds.h);
    }

    Widget* hit_test(Point pt) override {
        if (!bounds.contains(pt)) return nullptr;
        if (show_arrows) {
            if (pt.x < bounds.x + arrow_w) return this;
            if (pt.x > bounds.right() - arrow_w) return this;
        }
        Rect body = body_rect();
        if (index >= 0 && index < (int)pages.size() && body.contains(pt)) {
            if (Widget* w = pages[(size_t)index]->hit_test(pt)) return w;
        }
        return this;
    }
    void on_click_at(Point pt) override {
        if (show_arrows) {
            if (pt.x < bounds.x + arrow_w) { set_index(index - 1); return; }
            if (pt.x > bounds.right() - arrow_w) { set_index(index + 1); return; }
        }
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        p.push_clip(body_rect());
        if (index >= 0 && index < (int)pages.size()) pages[(size_t)index]->paint(p);
        p.pop_clip();
        if (!pages.empty()) {
            std::wstring pg = std::to_wstring(index + 1) + L" / " + std::to_wstring(pages.size());
            p.text(pg, Rect(bounds.x, bounds.bottom() - 18.f, bounds.w, 16.f), TextScale::Caption,
                   pal.text_tertiary, HAlign::Center, VAlign::Middle, false);
        }
        if (!show_arrows) return;
        float cy = bounds.y + bounds.h * 0.5f;
        bool can_prev = index > 0, can_next = index + 1 < (int)pages.size();
        p.glyph(glyph_prev, Rect(bounds.x + arrow_w * 0.5f - 9.f, cy - 9.f, 18.f, 18.f), 12.f,
                can_prev ? pal.text_secondary : pal.text_tertiary.alpha(0.4f));
        p.glyph(glyph_next, Rect(bounds.right() - arrow_w * 0.5f - 9.f, cy - 9.f, 18.f, 18.f),
                12.f, can_next ? pal.text_secondary : pal.text_tertiary.alpha(0.4f));
    }
};

// --------------------------------------------------------- ItemsRepeater ----
// 数据驱动的重复项容器：程序给一份字符串列表 + 一个 builder，库按间距排出子控件。
// 任何一条内容都不在库里，改数据就 refresh()。
class ItemsRepeater : public Widget {
public:
    std::vector<std::wstring> items;
    std::function<std::shared_ptr<Widget>(int, const std::wstring&)> build;
    std::vector<std::shared_ptr<Widget>> children;
    float spacing = 8.f;
    bool  horizontal = false;
    float item_h = 34.f;
    float padding = 0.f;

    template <class... Args>
    explicit ItemsRepeater(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        items = o.items;
        spacing = or_default(o.gap, 8.f);
        horizontal = o.wrap;
        item_h = or_default(o.height, 34.f);
        padding = or_default(o.padding, 0.f);
        flex = o.flex;
        hint_w = o.width;
    }

    // 重建子控件（数据变了就调它）
    void refresh() {
        children.clear();
        for (int i = 0; i < (int)items.size(); ++i) {
            if (!build) continue;
            auto w = build(i, items[(size_t)i]);
            if (!w) continue;
            if (host) w->attach(host);
            children.push_back(std::move(w));
        }
        if (host) host->request_repaint();
    }

    void on_attach() override {
        if (children.size() != items.size()) refresh();
        for (auto& c : children) c->attach(host);
    }

    Size measure(Painter& p, Size avail) override {
        if (children.size() != items.size()) refresh();
        float w = 0.f, h = padding * 2.f;
        if (horizontal) {
            for (size_t i = 0; i < children.size(); ++i) {
                Size cs = children[i]->measure(p, Size(1e6f, avail.h));
                w += std::isnan(children[i]->hint_w) ? cs.w : children[i]->hint_w;
                h = std::max(h, item_h + padding * 2.f);
                if (i + 1 < children.size()) w += spacing;
            }
            return Size(or_default(hint_w, w), or_default(hint_h, h));
        }
        float cw = avail.w;
        for (size_t i = 0; i < children.size(); ++i) {
            Size cs = children[i]->measure(p, Size(cw, 1e6f));
            w = std::max(w, cs.w);
            h += item_h;
            if (i + 1 < children.size()) h += spacing;
        }
        return Size(or_default(hint_w, w + padding * 2.f), or_default(hint_h, h));
    }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        float x = r.x + padding, y = r.y + padding;
        float inner = r.w - padding * 2.f;
        for (auto& c : children) {
            if (horizontal) {
                float cw = std::isnan(c->hint_w) ? c->measure(p, Size(1e6f, r.h)).w : c->hint_w;
                c->arrange(p, Rect(x, y, cw, item_h));
                x += cw + spacing;
            } else {
                c->arrange(p, Rect(x, y, inner, item_h));
                y += item_h + spacing;
            }
        }
    }

    Widget* hit_test(Point pt) override {
        if (!bounds.contains(pt)) return nullptr;
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if (Widget* w = (*it)->hit_test(pt)) return w;
        return nullptr;
    }

    void paint(Painter& p) override {
        for (auto& c : children) c->paint(p);
    }
};

inline void Widget::open_context_menu(Point pt) {
    if (!host || context_items.empty()) return;
    auto self = shared_from_this();
    auto panel = std::make_shared<PopupPanel>();
    panel->items = context_items;
    panel->row_h = 32.f;
    panel->radius = Radius::flyout;
    panel->cast_shadow = true;
    panel->show_check = false;
    panel->hover = -1;
    panel->on_pick = [self](int i) {
        if (self->on_context_pick) self->on_context_pick(i);
        if (self->host) {
            if (!self->change_name.empty()) self->host->fire(self->change_name, self.get(), true);
            self->host->close_popup();
            self->host->request_repaint();
        }
    };
    panel->on_cancel = [self]() {
        if (self->host) self->host->request_repaint();
    };
    // 没有 Painter 可测字宽，按"中文按 12px、西文按 7px"粗估，够放下再留点余量
    float pw = 180.f;
    for (auto& s : context_items) {
        if (PopupPanel::is_separator(s)) continue;
        float w = 40.f;
        for (wchar_t c : s) w += (c < 0x80 ? 7.f : 12.f);
        pw = std::max(pw, w + 24.f);
    }
    float h = PopupPanel::content_height(context_items, 32.f, 4.f);
    Rect cs = host->client_screen_rect();
    float sc = host->dpi_scale();
    Rect screen_px(cs.x + pt.x * sc, cs.y + pt.y * sc, pw * sc, h * sc);
    host->show_popup(panel, screen_px, PopupDir::Down);
}

// 拆分 / 下拉按钮的菜单：贴着按钮下方弹一层，宽度至少和按钮一样，
// 项与项的排布、快捷键右对齐、分隔线全部交给 PopupPanel。
inline void Button::open_menu_() {
    if (!host || items.empty() || opened) return;
    float row_h = 36.f;
    float want_h = PopupPanel::content_height(items, row_h, 4.f);
    Rect region = host->client_rect();
    float pw = std::max(bounds.w, 160.f);
    float gap = 6.f;
    float below = region.bottom() - (bounds.bottom() + gap);
    float above = bounds.y - gap - region.y;
    float h = want_h;
    PopupDir pd = PopupDir::Down;
    if (below >= want_h) { pd = PopupDir::Down; }
    else if (above >= want_h) { pd = PopupDir::Up; }
    else { pd = (below >= above) ? PopupDir::Down : PopupDir::Up;
           h = std::max(row_h + 8.f, below >= above ? below : above); }

    auto self = std::static_pointer_cast<Button>(shared_from_this());
    panel = std::make_shared<PopupPanel>();
    panel->items = items;
    panel->selected = -1;
    panel->row_h = row_h;
    panel->radius = Radius::flyout;
    panel->cast_shadow = true;
    panel->show_check = false;
    panel->hover = -1;
    panel->on_pick = [self](int i) {
        self->opened = false;
        if (self->on_pick) self->on_pick(i);
        if (self->host) {
            if (!self->change_name.empty()) self->host->fire(self->change_name, self.get(), true);
            self->host->close_popup();
            self->host->request_repaint();
        }
    };
    panel->on_cancel = [self]() {
        self->opened = false;
        if (self->host) self->host->request_repaint();
    };
    Rect target(bounds.x, bounds.y + bounds.h + gap, pw, h);
    Rect cs = host->client_screen_rect();
    float s = host->dpi_scale();
    Rect screen_px(cs.x + target.x * s, cs.y + target.y * s, target.w * s, target.h * s);
    if (host->show_popup(panel, screen_px, pd)) {
        opened = true;
        host->request_repaint();
    } else {
        panel.reset();
    }
}

// -------------------------------------------------------------- ComboBox ----
// 下拉框：按钮样式的收拢态 + 弹出列表。展开方向按可用空间自动决定
// （下方够就往下、不够就往上、竖向都挤不下就向左/右），动画也跟着方向走。
class ComboBox : public Widget {
public:
    std::vector<std::wstring> items;
    std::wstring placeholder;        // 没选中时显示的提示文字：库不自带，由调用方给
    int   selected = -1;
    bool  allow_overflow = false;   // 弹出层能不能超出窗口（默认不能）
    bool  menu = false;             // 菜单式（"文件(F)"这类）：只触发动作、不回填值
    bool  menubar = false;          // 菜单栏式触发：只有文字（无边框、无箭头），点开就是菜单
    bool  hover_open = true;        // 子菜单悬浮是否自动展开
    double hover_delay_ms = 0.0;    // 悬停多少毫秒后展开（0 = 立刻）
    float row_h = 36.f;
    float max_h = NAN;
    float radius = Radius::control;
    bool  opened = false;
    int   last_index = -1;          // 最近一次点击的行（菜单模式用）
    std::function<void(int)> on_action;

    std::shared_ptr<PopupPanel> panel;
    PopupDir dir = PopupDir::Down;

    template <class... Args>
    explicit ComboBox(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        items = o.items;
        placeholder = o.placeholder;
        selected = o.selected;
        allow_overflow = o.allow_overflow;
        menu = o.menu;
        menubar = o.menubar;
        if (menubar) menu = true;                 // 菜单栏触发一定不回填内容
        hover_open = o.hover_open < 0 ? true : (o.hover_open > 0);
        hover_delay_ms = or_default(o.hover_delay, 0.f);
        max_h = o.max_height;
        row_h = or_default(o.row_height, 36.f);
        radius = or_default(o.radius, Radius::control);
        hint_w = o.width;
        hint_h = o.height;
        flex = o.flex;
        enabled = o.enabled;
        cast_shadow = shadow_opt(o, true);   // 下拉框默认有阴影（和菜单一样要分层级）
        click_name = o.click;
        change_name = o.change;
        pending_open_ = o.open;
        // kw::value("某项")：按文本选中
        if (selected < 0 && !o.text.empty()) {
            for (size_t i = 0; i < items.size(); ++i) {
                if (items[i] == o.text) { selected = (int)i; break; }
            }
        }
        if (selected < 0 && !items.empty() && !pending_open_) selected = -1;
    }

    bool interactive() const override { return enabled; }
    bool checked_state() const override { return opened; }

    std::wstring value() const {
        if (selected >= 0 && selected < (int)items.size()) return items[selected];
        return std::wstring();
    }

    void set_items(const std::vector<std::wstring>& v) {
        items = v;
        if (selected > (int)v.size() - 1) selected = -1;
        if (host) host->request_repaint();
    }

    void set_selected(int i, bool fire_change = true) {
        if (i < -1 || i >= (int)items.size()) return;
        selected = i;
        if (fire_change && host && !change_name.empty()) host->fire(change_name, this, true);
        if (host) host->request_repaint();
    }

    Size measure(Painter& p, Size avail) override {
        float tw = 0.f;
        for (auto& s : items) tw = std::max(tw, p.measure_text(s, TextScale::Body).w);
        if (!placeholder.empty())
            tw = std::max(tw, p.measure_text(placeholder, TextScale::Body).w);
        if (menubar) {
            // 菜单栏按钮：宽度就是文字宽度 + 一点内边距，不撑成输入框
            float lw = p.measure_text(placeholder, TextScale::Body).w + 24.f;
            if (lw < 44.f) lw = 44.f;
            // 菜单栏点开后的面板宽度：菜单里最长的一项（含右侧快捷键）撑出来的宽度。
            // 这段必须在下面的早返回之前算，否则面板只有 160 宽，中文会和快捷键压到一起。
            float need = 150.f;
            for (auto& s : items) {
                if (PopupPanel::is_separator(s)) continue;
                std::wstring label, accel;
                PopupPanel::split_item(s, label, accel);
                float w = p.measure_text(label, TextScale::Body).w + 18.f + 12.f;
                if (!accel.empty()) w += p.measure_text(accel, TextScale::Body).w + 12.f;
                need = std::max(need, w + 44.f);
            }
            panel_w_hint_ = need;
            return Size(or_default(hint_w, lw), or_default(hint_h, 30.f));
        }
        // 面板宽度：菜单里最长的一项（含右侧快捷键）撑出来的宽度
        float need = 150.f;
        for (auto& s : items) {
            std::wstring label, accel;
            PopupPanel::split_item(s, label, accel);
            float w = p.measure_text(label, TextScale::Body).w + 18.f + 12.f;
            if (!accel.empty()) w += p.measure_text(accel, TextScale::Body).w + 12.f;
            need = std::max(need, w + 40.f);
        }
        panel_w_hint_ = need;
        float w = or_default(hint_w, std::min(avail.w > 0.f ? avail.w : 1e6f, tw + 72.f));
        if (w < 120.f) w = 120.f;
        return Size(w, or_default(hint_h, 32.f));
    }

    void paint(Painter& p) override {
        tick_anim();
        double now = now_ms();
        float hv = hover_t.at(now);
        float pv = press_t.at(now);
        const Palette& pal = p.palette();

        if (menubar) {
            // 菜单栏触发（Typedown 的"文件 编辑 段落 …"）：只有文字。
            // 鼠标移上去 / 菜单开着的时候才有一块很淡的底，不画框、不画箭头、不写值。
            float lit = std::max(hv, opened ? 1.f : 0.f);
            if (lit > 0.01f) {
                Color c = pal.flyout_hover;
                c.a *= (opened ? 0.85f : 0.5f) * lit;
                p.fill_round(bounds, 4.f, c);
            }
            p.text(placeholder, Rect(bounds.x + 10.f, bounds.y, bounds.w - 16.f, bounds.h),
                   TextScale::Body, enabled ? pal.text : pal.text_tertiary,
                   HAlign::Start, VAlign::Middle, false);
            return;
        }

        Color fill = !enabled ? (pal.dark ? pal.control.alpha(0.35f) : pal.control.alpha(0.55f))
                              : state_color(pal.control, pal.control_hover, pal.control_press, hv, pv);
        if (cast_shadow) p.shadow(bounds, radius, shadow_blur(), pal.shadow, shadow_offset());
        p.fill_round(bounds, radius, fill);
        p.stroke_round(bounds.inset(0.5f), radius,
                       enabled ? pal.control_stroke : pal.stroke_soft, 1.f);

        std::wstring shown = menu ? placeholder : value();
        bool has_value = menu || !shown.empty();
        if (!has_value) shown = placeholder;
        Color tc = !enabled ? pal.text_tertiary : (has_value ? pal.text : pal.text_tertiary);
        p.text(shown, Rect(bounds.x + 12.f, bounds.y, bounds.w - 44.f, bounds.h),
               TextScale::Body, tc, HAlign::Start, VAlign::Middle, false);
        // 展开时箭头翻上来（Win11 的观感）
        p.glyph(opened ? L"\uE70E" : L"\uE70D",
                Rect(bounds.right() - 32.f, bounds.y, 20.f, bounds.h), 11.f,
                enabled ? pal.text_secondary : pal.text_tertiary);
    }

    void on_attach() override {
        if (pending_open_) {
            pending_open_ = false;
            open_();
        }
    }

    // 面板刚造好时回调一次：挂子菜单、改行高/勾选样式等
    std::function<void(PopupPanel&)> on_panel;

    void on_click() override {
        if (!enabled) return;
        if (opened) {
            close_();
        } else {
            open_();
        }
        if (host && !click_name.empty()) host->fire(click_name, this, opened);
    }

    // ---- 展开 / 收起 ----
    void open_() {
        if (!host || items.empty()) return;
        Rect target = compute_rect_();
        if (target.w <= 0.f || target.h <= 0.f) return;

        auto self = std::static_pointer_cast<ComboBox>(shared_from_this());
        panel = std::make_shared<PopupPanel>();
        panel->items = items;
        panel->selected = selected;
        panel->row_h = row_h;
        panel->radius = Radius::flyout;
        panel->cast_shadow = cast_shadow;
        panel->show_check = !menu;
        panel->hover = -1;
        panel->hover_open = hover_open;
        panel->hover_delay_ms = hover_delay_ms;
        panel->hover_open_mode = hover_open ? 1 : 0;
        // 这一套（含下面每一层子菜单）都用同一个"悬浮是否自动开 / 悬停多久"
        if (host) host->set_popup_hover(hover_open, hover_delay_ms);
        panel->on_pick = [self](int i) {
            self->last_index = i;
            self->opened = false;
            if (self->menu) {
                // 菜单式：不动 selected，只把动作派发出去
                if (self->on_action) self->on_action(i);
            } else {
                self->selected = i;
            }
            if (self->host) {
                if (!self->change_name.empty()) self->host->fire(self->change_name, self.get(), true);
                self->host->close_popup();
                self->host->request_repaint();
            }
        };
        panel->on_cancel = [self]() {
            self->opened = false;
            if (self->host) self->host->request_repaint();
        };
        // 逻辑坐标 -> 屏幕物理像素
        if (on_panel) on_panel(*panel);
        Rect cs = host->client_screen_rect();
        float s = host->dpi_scale();
        Rect screen_px(cs.x + target.x * s, cs.y + target.y * s, target.w * s, target.h * s);
        if (host->show_popup(panel, screen_px, dir)) {
            opened = true;
            if (host) host->request_repaint();
        } else {
            panel.reset();
        }
    }
    void close_() {
        opened = false;
        if (host) host->close_popup();
        if (host) host->request_repaint();
    }

    // 触发点在「允许超出窗口」时的可用区域：显示器工作区；否则就是客户区。
    Rect work_region_() const {
        Rect client = host->client_rect();
        if (!allow_overflow) return client;
        Rect work = host->monitor_work_rect();
        Rect cs = host->client_screen_rect();
        float s = host->dpi_scale();
        if (s <= 0.f) s = 1.f;
        return Rect((work.x - cs.x) / s, (work.y - cs.y) / s, work.w / s, work.h / s);
    }

    // 计算弹出面板的最终矩形（逻辑坐标，相对客户区左上角）
    Rect compute_rect_() {
        Rect region = work_region_();
        float gap = menubar ? 2.f : 6.f;   // 菜单栏的菜单是贴着按钮底下弹出来的
        float pw = menubar ? std::max(panel_w_hint_, 160.f)
                           : (bounds.w < 140.f ? 140.f : bounds.w);

        std::vector<std::wstring> visible_items = items;
        if (!std::isnan(max_h)) {
            float acc = 4.f;
            size_t keep = 0;
            for (size_t i = 0; i < items.size(); ++i) {
                float ih = PopupPanel::item_height(items[i], row_h);
                if (acc + ih + 4.f > max_h) break;
                acc += ih;
                keep = i + 1;
            }
            if (keep < 1) keep = 1;
            visible_items.assign(items.begin(), items.begin() + (ptrdiff_t)keep);
        }
        float want_h = PopupPanel::content_height(visible_items, row_h, 4.f);
        float min_h = row_h + 8.f;

        float below = region.bottom() - (bounds.bottom() + gap);
        float above = bounds.y - gap - region.y;
        float right = region.right() - (bounds.right() + gap);
        float left = bounds.x - gap - region.x;

        float h = want_h;
        if (below >= want_h) {
            dir = PopupDir::Down;
        } else if (above >= want_h) {
            dir = PopupDir::Up;
        } else if (below >= min_h || above >= min_h) {
            // 两侧都不够：选更宽裕的一侧，并把高度压到放得下几行
            dir = below >= above ? PopupDir::Down : PopupDir::Up;
            h = std::min(want_h, below >= above ? below : above);
        } else if (right >= pw || left >= pw) {
            // 竖向真的挤不下，改从侧面拉出
            dir = right >= left ? PopupDir::Right : PopupDir::Left;
            h = std::min(want_h, region.h);
        } else {
            dir = below >= above ? PopupDir::Down : PopupDir::Up;
            h = std::max(min_h, below >= above ? below : above);
        }

        // 高度对齐到完整的项，避免出现半行
        h = PopupPanel::fit_height(visible_items, row_h, 4.f, h);

        Rect r;
        switch (dir) {
            case PopupDir::Down:
                r = Rect(bounds.x, bounds.bottom() + gap, pw, h);
                break;
            case PopupDir::Up:
                r = Rect(bounds.x, bounds.y - gap - h, pw, h);
                break;
            case PopupDir::Right:
                r = Rect(bounds.right() + gap, bounds.y, pw, h);
                break;
            default:   // Left
                r = Rect(bounds.x - gap - pw, bounds.y, pw, h);
                break;
        }

        // 夹回可用区域（允许超出窗口时这里就是显示器范围）
        if (r.right() > region.right()) r.x = region.right() - r.w;
        if (r.x < region.x) r.x = region.x;
        if (r.bottom() > region.bottom()) r.y = region.bottom() - r.h;
        if (r.y < region.y) r.y = region.y;
        popup_rect = r;
        return r;
    }

public:
    Rect popup_rect;   // 上次算出的弹出层矩形（逻辑坐标，相对客户区），调试/截图用

private:
    bool pending_open_ = false;
    float panel_w_hint_ = 160.f;

public:
    // 弹出面板至少要这么宽（菜单栏按钮点开时用；离屏出图也照它摆）
    float panel_width_hint() const { return panel_w_hint_; }
};

// ------------------------------------------------------------- IconList ----
// 列表：一行一个"图标 + 文字"，常显、**没有下拉特性**（不弹、不收、没有箭头）。
// 图标/文字/选中项全由程序给；要链接色就把 role 设成 Role::Accent。
// 放进 BlockBox 里就是 Win11 设置页那种「关于」块下面的许可条款列表。
class IconList : public Widget {
public:
    struct Row {
        std::wstring glyph;   // 左侧图标（空 = 只留缩进）
        std::wstring text;
    };

    std::vector<Row> rows;
    int   selected = -1;      // -1 = 没有选中项
    int   hover    = -1;
    float row_h    = 40.f;
    float pad      = 4.f;     // 列表自身上下留白
    float indent   = 12.f;    // 文字离行左边缘
    float glyph_w  = 18.f;    // 图标列宽（给 0 就是纯文字列表）
    float text_gap = 10.f;    // 图标到文字
    Role  role     = Role::Primary;   // 文字颜色（Accent = 链接样）
    bool  show_hover  = true;
    bool  show_select = true;
    // 链接 / 动作类列表设 false：点一下只派发 on_pick，不留选中态（本来就是"点了就跳走"的东西）
    bool  selectable  = true;
    bool  radio       = false;   // 单选圆点样式：不画整行选中底，靠前缀圆点表示选中

    std::function<void(int)> on_pick;

    template <class... Args>
    explicit IconList(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        if (!o.items.empty()) {
            for (auto& s : o.items) rows.push_back(Row{L"", s});
        }
        if (!o.icon.empty()) for (auto& r : rows) r.glyph = o.icon;
        selected = o.selected;
        role = o.role;
        row_h = or_default(o.height, 40.f);
        hint_w = o.width;
    }

    // 程序加行的两个入口（图标、文字全自己给）
    bool interactive() const override { return enabled; }

    IconList& add_row(const std::wstring& text, const std::wstring& glyph = L"") {
        rows.push_back(Row{glyph, text});
        if (host) host->request_repaint();
        return *this;
    }

    Size measure(Painter& p, Size avail) override {
        (void)p;
        float h = pad * 2.f + row_h * (float)rows.size();
        float w = or_default(hint_w, avail.w);
        return Size(w, or_default(hint_h, h));
    }

    void arrange(Painter& p, const Rect& r) override {
        (void)p;
        bounds = r;
    }

    Rect row_rect(int i) const {
        return Rect(bounds.x, bounds.y + pad + (float)i * row_h, bounds.w, row_h);
    }

    void paint(Painter& p) override {
        const Palette& pal = p.palette();
        Color ink = pal.text_for(role);
        for (int i = 0; i < (int)rows.size(); ++i) {
            Rect rr = row_rect(i);
            Rect inner = rr.inset(2.f, 0.f);
            bool sel = selectable && show_select && i == selected;
            if (!radio && sel) {
                // 选中只是一层很淡的灰底 + 左侧一根 accent 短竖条，
                // 不用整行蓝底（链接类列表看起来像被选中很奇怪）
                p.fill_round(inner, Radius::control, pal.flyout_hover);
                p.fill_round(Rect(inner.x + 2.f, inner.y + 6.f, 3.f, inner.h - 12.f), 1.5f, pal.accent);
            } else if (show_hover && i == hover) {
                p.fill_round(inner, Radius::control, pal.flyout_hover);
            }
            float x = rr.x + indent;
            if (radio) {
                // 单选圆点（Win11 设置里"浅色 / 深色"那种）：选中是 accent 实心圆 + 中心一点底色
                float d = 16.f;
                Rect dot(x, rr.y + (rr.h - d) * 0.5f, d, d);
                if (sel) {
                    p.fill_round(dot, d * 0.5f, pal.accent);
                    p.fill_round(dot.inset(5.5f, 5.5f), 2.5f, pal.card);
                } else {
                    p.stroke_round(dot, d * 0.5f, pal.text_secondary, 1.4f);
                }
                x += d + 10.f;
            }
            if (glyph_w > 0.f && !rows[(std::size_t)i].glyph.empty()) {
                p.glyph(rows[(std::size_t)i].glyph,
                        Rect(x, rr.y + (rr.h - glyph_w) * 0.5f, glyph_w, glyph_w),
                        glyph_w * 0.75f, ink);
            }
            x += (glyph_w > 0.f ? glyph_w : 0.f) + text_gap;
            p.text(rows[(std::size_t)i].text,
                   Rect(x, rr.y, std::max(0.f, rr.right() - x - indent), rr.h),
                   TextScale::Body, ink, HAlign::Start, VAlign::Middle, false);
        }
    }

    int row_at(Point pt) const {
        if (rows.empty()) return -1;
        int i = (int)((pt.y - bounds.y - pad) / row_h);
        if (i < 0 || i >= (int)rows.size()) return -1;
        return row_rect(i).contains(pt) ? i : -1;
    }

    void on_move(Point pt) override {
        if (!show_hover) return;
        int i = row_at(pt);
        if (i != hover) { hover = i; if (host) host->request_repaint(); }
    }
    void on_hover(bool on) override {
        Widget::on_hover(on);
        if (!on && hover != -1) { hover = -1; if (host) host->request_repaint(); }
    }
    void on_click_at(Point pt) override {
        int i = row_at(pt);
        if (i < 0) return;
        if (selectable) selected = i;
        if (host) host->request_repaint();
        if (on_pick) on_pick(i);
        if (host && !change_name.empty()) host->fire(change_name, this, true);
    }
};

// -------------------------------------------------------------- BlockBox ----
// 块装盒：一个圆角块里装若干控件（"两个合并"成一个整块），块内子项之间用一条分隔线断开。
// 头部可以带图标 + 标题 + 副标题 + 说明行，可展开/收起；所有文字都由程序给，空就不占位。
class BlockBox : public Widget {
public:
    std::vector<std::shared_ptr<Widget>> children;

    std::wstring glyph;        // 头部左侧图标（空 = 不留那一列）
    std::wstring title;        // 主标题
    std::wstring subtitle;     // 副标题（小字，次要色）
    std::wstring note;         // 第三行（版本号之类）

    bool  expandable = false;  // 头部可点，展开/收起下面的子项
    bool  expanded   = true;
    // 展开/收起的动画进度：0 = 收起，1 = 展开。直接改 expanded 也会自动距离归位
    float ease_      = 1.f;
    float anim_from_ = 1.f;
    double anim_t0_  = -1.0;
    // 开合时长：比下拉框（320ms）快一档。块装盒是"内容整体滑进来"，同一条曲线下
    // 会显得比下拉框更拖，所以这里自己压到 220ms，并且展开曲线换成纯 ease-out
    // （起步就是满速，头一帧就明显在动；bezier(.2,.5,.55,1) 前 1/4 几乎不动，最像"慢半拍"）。
    float  open_ms  = 220.f;
    float  close_ms = 140.f;
    bool  pin_ease   = false;   // 出图用：钉住 ease_，不自己跑
    bool  closable   = false;  // 右上角 ×（图标，不是文字）
    bool  separators = true;   // 多个子项之间画分隔线

    std::wstring glyph_expand   = L"\uE70D";   // 收起状态（朝下）
    std::wstring glyph_collapse = L"\uE70E";   // 展开状态（朝上）
    std::wstring glyph_close    = L"\uE711";

    float radius    = Radius::card;
    float pad       = 16.f;
    float head_pad  = 14.f;    // 头部上下留白
    float title_h   = 22.f;
    float sub_h     = 18.f;
    float note_h    = 18.f;
    float icon_size = 24.f;
    float spacing   = 10.f;    // 头部与内容、内容行与行之间

    std::function<void(bool)> on_toggle;
    std::function<void()>     on_close;

    float head_h() const {
        float t = 0.f;
        if (!title.empty())    t += title_h;
        if (!subtitle.empty()) t += sub_h;
        if (!note.empty())     t += note_h;
        if (t > 0.f) t += head_pad * 2.f;
        return t;
    }

    // 当前可见比例（不可折叠的块永远是 1）
    float e_now() const { return expandable ? std::clamp(ease_, 0.f, 1.f) : 1.f; }

    void tick_ease() {
        if (pin_ease) return;
        float target = expanded ? 1.f : 0.f;
        if (anim_t0_ < 0.0) {
            if (std::abs(ease_ - target) < 0.001f) { ease_ = target; return; }
            anim_t0_ = now_ms();          // 外部直接赋值了 expanded 也跟着走
            anim_from_ = ease_;
        }
        bool opening = target > anim_from_;
        double dur = opening ? open_ms : close_ms;
        float k = dur > 0.0 ? (float)((now_ms() - anim_t0_) / dur) : 1.f;
        if (k < 0.f) k = 0.f;
        if (k >= 1.f) { k = 1.f; anim_t0_ = -1.0; }
        // ease_popup_close(k) 是"可见比例"（1→0），当进度用要取 1-它
        float p = opening ? ease_out_cubic(k) : (1.f - ease_popup_close(k));
        ease_ = anim_from_ + (target - anim_from_) * p;
        if (host) { host->mark_animating(); host->request_repaint(); }
    }

    void set_expanded(bool on) {
        if (expanded == on) return;
        anim_t0_ = now_ms();
        anim_from_ = ease_;
        expanded = on;
        if (host) { host->request_repaint(); host->mark_animating(); }
        if (on_toggle) on_toggle(expanded);
    }

    // 出图用：把展开进度钉在中间帧
    void pin_expand_e(float e) { pin_ease = true; ease_ = std::clamp(e, 0.f, 1.f); }

    template <class... Args>
    explicit BlockBox(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        title = o.title;
        subtitle = o.subtitle;
        expandable = false;          // 要折叠式按键自己设 expandable = true
        expanded = o.expanded;
        closable = o.closable;
        glyph = o.icon;
        hint_w = o.width;
        hint_h = o.height;
        flex = o.flex;
        cast_shadow = false;        // 块一般摆在卡片里，默认不叠第二层阴影
    }

    BlockBox& add(std::shared_ptr<Widget> w) {
        if (!w) return *this;
        if (host) w->attach(host);
        children.push_back(std::move(w));
        if (host) host->request_repaint();
        return *this;
    }

    void on_attach() override {
        for (auto& c : children) c->attach(host);
    }

    // 收起时 ease 还没归零，内容仍要参与布局、靠裁切滑走，而不是瞬间消失
    bool content_visible() const { return expandable ? (expanded || ease_ > 0.001f) : true; }

    Size measure(Painter& p, Size avail) override {
        float inner = avail.w - pad * 2.f;
        float h = pad * 2.f;
        float head = head_h();
        if (head > 0.f) h += head;
        float body = 0.f;
        for (size_t i = 0; i < children.size(); ++i) {
            Size cs = children[i]->measure(p, Size(inner, 1e6f));
            float ch = std::isnan(children[i]->hint_h) ? cs.h : children[i]->hint_h;
            body += ch + (i ? spacing : 0.f);
        }
        h += body * e_now();          // 高度跟着动画长
        return Size(or_default(hint_w, avail.w), or_default(hint_h, h));
    }

    void arrange(Painter& p, const Rect& r) override {
        bounds = r;
        float x = r.x + pad, w = r.w - pad * 2.f;
        float y = r.y + pad;
        float head = head_h();
        head_r = head > 0.f ? Rect(x, y, w, head) : Rect();
        y += head;
        body_top_ = y;
        body_total_ = 0.f;
        if (!content_visible()) return;

        // 先把每个子项的高度定下来，再按动画位置排（和下拉框同一套 reveal 方式）
        std::vector<float> hs(children.size(), 0.f);
        float total = 0.f;
        for (size_t i = 0; i < children.size(); ++i) {
            float ch = children[i]->hint_h;
            if (std::isnan(ch)) {
                Size cs = children[i]->measure(p, Size(w, 1e6f));
                ch = cs.h;
            }
            hs[i] = ch;
            total += ch + (i ? spacing : 0.f);
        }
        body_total_ = total;
        // 和下拉框同一套"拉帘"：可见窗顶边钉住、高度按 e 往下长，内容本身不动，
        // 展开时从上往下露出来（先看到第一行），收起来时从下往上被裁掉。
        float yy = body_top_;
        for (size_t i = 0; i < children.size(); ++i) {
            if (i) yy += spacing;
            children[i]->arrange(p, Rect(x, yy, w, hs[i]));
            yy += hs[i];
        }
    }

    // 动画期间"能看见、也能点"的那条竖窗
    Rect reveal_rect() const {
        float vis = body_total_ * e_now();
        return Rect(bounds.x + 1.f, body_top_, bounds.w - 2.f, vis);
    }

    Widget* hit_test(Point pt) override {
        // 展开/收起过程中子项在画面上是滑动的，命中还按最终位置算会对不上 —— 动画里只认头部
        if (content_visible() && e_now() > 0.9f && reveal_rect().contains(pt)) {
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                if ((*it)->bounds.contains(pt)) {
                    if (Widget* w = (*it)->hit_test(pt)) return w;
                }
        }
        if (bounds.contains(pt)) return this;
        return nullptr;
    }

    bool interactive() const override { return enabled && (expandable || closable); }

    void paint(Painter& p) override {
        tick_anim();
        tick_ease();
        const Palette& pal = p.palette();
        draw_shadow(p);
        p.fill_round(bounds, radius, pal.card);
        p.stroke_round(bounds.inset(0.5f), radius, pal.stroke_soft, 1.f);

        if (head_r.w > 0.f) {
            // 头部整行悬浮时整块提亮（视频里就是整行变亮）
            if (expandable && head_hover_) p.fill_round(head_r.inset(-4.f, -2.f), Radius::control, pal.flyout_hover);

            float tx = head_r.x;
            if (!glyph.empty() && icon_size > 0.f) {
                p.glyph(glyph, Rect(head_r.x, head_r.y + (head_r.h - icon_size) * 0.5f, icon_size, icon_size),
                        icon_size * 0.8f, pal.text_secondary);
                tx += icon_size + 12.f;
            }
            float tw = head_r.right() - tx;
            if (expandable || closable) tw -= 28.f;

            float ty = head_r.y + head_pad;
            if (!title.empty()) {
                p.text(title, Rect(tx, ty, tw, title_h), TextScale::Body, pal.text,
                       HAlign::Start, VAlign::Middle, false);
                ty += title_h;
            }
            if (!subtitle.empty()) {
                p.text(subtitle, Rect(tx, ty, tw, sub_h), TextScale::Caption, pal.text_tertiary,
                       HAlign::Start, VAlign::Middle, false);
                ty += sub_h;
            }
            if (!note.empty()) {
                p.text(note, Rect(tx, ty, tw, note_h), TextScale::Caption, pal.text_tertiary,
                       HAlign::Start, VAlign::Middle, false);
            }
            if (expandable) {
                Rect cr(head_r.right() - 24.f, head_r.y + (head_r.h - 20.f) * 0.5f, 20.f, 20.f);
                p.glyph(e_now() > 0.5f ? glyph_collapse : glyph_expand, cr, 12.f, pal.text_secondary);
            }
            if (closable) {
                Rect cr(head_r.right() - 24.f, head_r.y + (head_r.h - 20.f) * 0.5f, 20.f, 20.f);
                if (expandable) cr.x -= 26.f;
                p.glyph(glyph_close, cr, 12.f, close_hover_ ? pal.text : pal.text_tertiary);
            }
        }

        // 子项区：高度按 e 往下长，同时内容整体从上方滑进来（Win11 计算器"关于"那块的实际动效）。
        // 只露下沿、内容不动的那种"拉帘"会先露出第一行，观感是从上往下揭；这里反过来 ——
        // 打开时最后一行先到位、第一行最后落进来，收起时内容整体被往上抽走。
        Rect rv = reveal_rect();
        if (rv.h <= 0.5f) return;
        p.push_clip(rv);
        float e = e_now();
        p.push_translate(0.f, -(1.f - e) * body_total_);
        for (size_t i = 0; i < children.size(); ++i) {
            children[i]->paint(p);
            if (separators && i + 1 < children.size()) {
                float ly = children[i]->bounds.bottom() + spacing * 0.5f;
                p.fill_rect(Rect(bounds.x + pad, ly, bounds.w - pad * 2.f, 1.f), pal.flyout_sep);
            }
        }
        p.pop_translate();
        p.pop_clip();
    }

    void on_move(Point pt) override {
        bool on = expandable && head_r.w > 0.f && head_r.contains(pt);
        if (on != head_hover_) { head_hover_ = on; if (host) host->request_repaint(); }
        bool cl = closable && head_r.w > 0.f && close_rect().contains(pt);
        if (cl != close_hover_) { close_hover_ = cl; if (host) host->request_repaint(); }
    }
    void on_hover(bool on) override {
        Widget::on_hover(on);
        if (!on && (head_hover_ || close_hover_)) {
            head_hover_ = close_hover_ = false;
            if (host) host->request_repaint();
        }
    }
    void on_click_at(Point pt) override {
        if (closable && close_rect().contains(pt)) { if (on_close) on_close(); return; }
        if (expandable && head_r.w > 0.f && head_r.contains(pt)) set_expanded(!expanded);
    }

    // 自测用
    Rect head_rect() const { return head_r; }
    bool head_hover() const { return head_hover_; }

private:
    Rect close_rect() const {
        if (!closable || head_r.w == 0.f) return Rect();
        float x = head_r.right() - 24.f;
        if (expandable) x -= 26.f;
        return Rect(x, head_r.y + (head_r.h - 20.f) * 0.5f, 20.f, 20.f);
    }
    Rect  head_r;
    float body_top_ = 0.f, body_total_ = 0.f;
    bool  head_hover_ = false, close_hover_ = false;
};


namespace ui {

template <class... Args>
std::shared_ptr<Label> label(Args&&... args) {
    return std::make_shared<Label>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Button> button(Args&&... args) {
    return std::make_shared<Button>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Toggle> toggle(Args&&... args) {
    return std::make_shared<Toggle>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<ProgressRing> ring(Args&&... args) {
    return std::make_shared<ProgressRing>(std::forward<Args>(args)...);
}
// 名字用 bar：ring 已被进度环占了
template <class... Args>
std::shared_ptr<ProgressBar> bar(Args&&... args) {
    return std::make_shared<ProgressBar>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Divider> divider(Args&&... args) {
    return std::make_shared<Divider>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<ComboBox> combo(Args&&... args) {
    return std::make_shared<ComboBox>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<TextBox> textbox(Args&&... args) {
    return std::make_shared<TextBox>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<NumberBox> number(Args&&... args) {
    return std::make_shared<NumberBox>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Image> image(Args&&... args) {
    return std::make_shared<Image>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Rating> rating(Args&&... args) {
    return std::make_shared<Rating>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Badge> badge(Args&&... args) {
    return std::make_shared<Badge>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<InfoBar> infobar(Args&&... args) {
    return std::make_shared<InfoBar>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Expander> expander(Args&&... args) {
    return std::make_shared<Expander>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<SelectorBar> selectorbar(Args&&... args) {
    return std::make_shared<SelectorBar>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<PipsPager> pips(Args&&... args) {
    return std::make_shared<PipsPager>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<BreadcrumbBar> breadcrumb(Args&&... args) {
    return std::make_shared<BreadcrumbBar>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<AutoSuggestBox> autosuggest(Args&&... args) {
    return std::make_shared<AutoSuggestBox>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<SplitView> splitview(Args&&... args) {
    return std::make_shared<SplitView>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<FlipView> flipview(Args&&... args) {
    return std::make_shared<FlipView>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<ItemsRepeater> repeater(Args&&... args) {
    return std::make_shared<ItemsRepeater>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<NavigationView> navview(Args&&... args) {
    return std::make_shared<NavigationView>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Dialog> dialog(Args&&... args) {
    return std::make_shared<Dialog>(std::forward<Args>(args)...);
}
inline std::shared_ptr<Spacer> spacer(float h) {
    return std::make_shared<Spacer>(kw::height(h));
}
template <class... Args>
std::shared_ptr<Stack> column(Args&&... args) {
    return std::make_shared<Stack>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<Stack> row(Args&&... args) {
    auto s = std::make_shared<Stack>(std::forward<Args>(args)...);
    s->horizontal = true;
    s->stretch_children = false;
    return s;
}
template <class... Args>
std::shared_ptr<Card> card(Args&&... args) {
    return std::make_shared<Card>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<IconList> iconlist(Args&&... args) {
    return std::make_shared<IconList>(std::forward<Args>(args)...);
}
template <class... Args>
std::shared_ptr<BlockBox> blockbox(Args&&... args) {
    return std::make_shared<BlockBox>(std::forward<Args>(args)...);
}

}  // namespace ui

}  // namespace w11ui
