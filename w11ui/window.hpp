// w11ui / window.hpp -----------------------------------------------------------
// Scene（控件树宿主 + 事件分发）与 Window（Win32 窗口、DWM 圆角/深色标题栏、
// 60fps 补帧时钟、DPI 缩放）。
#pragma once

#include "core.hpp"
#include "theme.hpp"
#include "kwargs.hpp"
#include "painter.hpp"
#include "widgets.hpp"
#include "mnicompt.hpp"
#include "snapshot.hpp"

#include <dwmapi.h>
#include <mmsystem.h>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace w11ui {

// 光标资源：IDC_ARROW / IDC_HAND 是 MAKEINTRESOURCE 宏，宽字符版需要显式转换。
inline HCURSOR cursor_arrow() {
    static HCURSOR c = LoadCursorW(nullptr, (LPCWSTR)(ULONG_PTR)32512);
    return c;
}
inline HCURSOR cursor_hand() {
    static HCURSOR c = LoadCursorW(nullptr, (LPCWSTR)(ULONG_PTR)32649);
    return c;
}
// I 型光标：IDC_IBEAM = 32513，输入框上用它
inline HCURSOR cursor_ibeam() {
    static HCURSOR c = LoadCursorW(nullptr, (LPCWSTR)(ULONG_PTR)32513);
    return c;
}

// ------------------------------------------------------------- PopupWin ----
// 下拉框的弹出层：一个 WS_EX_LAYERED|WS_EX_NOACTIVATE 的独立窗口。
//   · 用 Direct2D DCRenderTarget 画到 32bpp DIB，再 UpdateLayeredWindow 提交 —— 这样
//     圆角外的像素和投影都能真正透明，不像普通子窗口那样必须有个方块底。
//   · 窗口矩形固定为「面板 + 阴影留白」，展开动画只改裁剪矩形，
//     于是「向下从顶边撑开 / 向上从底边撑开 / 左右从侧边撑开」都是同一条公式。
class PopupWin {
public:
    ~PopupWin() { destroy(); }

    bool visible() const { return hwnd_ && shown_ && !closing_; }
    HWND window() const { return hwnd_; }

    // 给「把当前界面存成 PNG」用：正在展开时返回面板和它的逻辑矩形
    void preview_target(std::shared_ptr<PopupPanel>& p, Rect& r) const {
        if (!visible() || !panel_) return;
        p = panel_;
        r = panel_logical_;
    }

    // panel_px：面板最终屏幕矩形（物理像素）；panel_logical：同一块区域在客户区逻辑坐标里的位置
    bool show(HWND owner, std::shared_ptr<PopupPanel> panel, const Rect& panel_px,
              const Rect& panel_logical, PopupDir dir, const Palette& pal, float scale,
              bool show_it = true) {
        if (!panel || panel_px.w <= 1.f || panel_px.h <= 1.f) return false;
        hide_now(false);

        owner_ = owner;
        panel_ = panel;
        // 面板自己要求重画时（hover 变化）直接重绘分层表面，不需要主窗口参与
        panel_->on_dirty = [this]() {
            art_dirty_ = true;
            if (shown_ && !closing_) render();
        };
        dir_ = dir;
        pal_ = pal;
        scale_ = scale > 0.f ? scale : 1.f;
        panel_px_ = panel_px;
        panel_logical_ = panel_logical;
        // 留白只为阴影存在，而且只留在右/下：画家把阴影往 +x/+y 偏 2.5px、再向外扩 blur，
        // 所以左/上边一分不留（面板左上角 = 窗口左上角，锚在父行顶边时严丝合缝），
        // 右/下留 offset+blur 给阴影。不投影的弹层（菜单、子菜单默认）四边全 0，窗口 = 面板本身。
        float sh = (panel_ && panel_->cast_shadow)
                     ? std::max(panel_->shadow_offset(), 0.f) + panel_->shadow_blur()
                     : 0.f;
        pad_rb_ = (int)std::lround(sh * scale_);

        if (!create()) return false;

        int w = (int)std::lround(panel_px.w) + pad_rb_;
        int h = (int)std::lround(panel_px.h) + pad_rb_;
        int x = (int)std::lround(panel_px.x);
        int y = (int)std::lround(panel_px.y);
        SetWindowPos(hwnd_, HWND_TOP, x, y, w, h,
                     SWP_NOACTIVATE | (show_it ? SWP_SHOWWINDOW : 0));
        shown_ = true;
        closing_ = false;
        headless_ = !show_it;   // 自测：别去跟踪真实鼠标，否则会立刻收到 MOUSELEAVE
        need_full_ulw_ = true;
        t0_ = now_ms();
        render();
        start_anim_clock();
        return true;
    }

    // 带动画收起
    void request_close() {
        if (!shown_ || closing_) return;
        closing_ = true;
        t0_ = now_ms();
        start_anim_clock();
    }

    // 立即消失（窗口移动/缩放/切主题时用，动画留着会错位）
    void hide_now(bool notify = true) {
        if (hwnd_) {
            stop_anim_clock();
            ShowWindow(hwnd_, SW_HIDE);
        }
        bool had = shown_;
        shown_ = false;
        closing_ = false;
        auto p = panel_;
        panel_.reset();
        if (notify && had && p && p->on_cancel) p->on_cancel();
    }

    void destroy() {
        hide_now(false);
        if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
        release_surface();
    }

    void set_palette(const Palette& pal) {
        pal_ = pal;
        art_dirty_ = true;
        if (visible()) render();
    }

    std::shared_ptr<PopupPanel> panel() const { return panel_; }
    const Rect& panel_px() const { return panel_px_; }

    // 面板在屏幕上的实际矩形（去掉阴影留白后，用来核对子菜单贴得对不对）
    Rect screen_panel_rect() const {
        RECT wr;
        if (!hwnd_ || !GetWindowRect(hwnd_, &wr)) return Rect();
        float p = (float)pad_rb_;   // 留白只在右/下
        return Rect((float)wr.left, (float)wr.top,
                    (float)(wr.right - wr.left) - p, (float)(wr.bottom - wr.top) - p);
    }

    // 第 row 行在屏幕上的物理像素矩形 —— 子菜单就贴着它往右展开
    Rect row_screen_px(int row) const {
        RECT wr;
        if (!hwnd_ || !panel_ || row < 0 || row >= (int)panel_->items.size()) return Rect();
        if (!GetWindowRect(hwnd_, &wr)) return Rect();
        Rect r = panel_->row_rect(row);
        return Rect((float)wr.left + r.x * scale_, (float)wr.top + r.y * scale_,
                    r.w * scale_, r.h * scale_);
    }

    // 分层表面像素（32bpp 预乘 BGRA，top-down），调试/视觉回归用
    const void* pixels(int& w, int& h) const {
        w = surf_w_;
        h = surf_h_;
        return bits_;
    }

    // 离屏连拍用：给定展开进度直接出一帧（-1 = 按真实时间走）
    void set_e_override(float e) {
        e_override = e;
        if (e_override >= 0.f && hwnd_) render();
    }

    // 每次出帧的时间戳（诊断"卡不卡"用）
    const std::vector<double>& frame_times() const { return frames_; }
    // 诊断"速度曲线到底长什么样"：当前进度 + 当前可见高度（像素）
    float reveal_e_now() const { return anim_e(now_ms()); }
    float visible_h_px_now() const {
        if (!panel_) return 0.f;
        Rect pr(0.f, 0.f, panel_px_.w / scale_, panel_px_.h / scale_);
        Rect win = reveal_win(pr, dir_, anim_e(now_ms()));
        return win.h * scale_;
    }
    double dbg_draw_ms() const { return dbg_draw_ms_; }
    double dbg_ulw_ms() const { return dbg_ulw_ms_; }
    // 画拆三段：begin（清底）/ blit（贴图+裁剪）/ end（EndDraw）
    std::tuple<double, double, double> dbg_draw_parts() const {
        return {dbg_begin_ms_, dbg_blit_ms_, dbg_end_ms_};
    }
    bool   shown_for_anim() const { return shown_; }
    bool   is_closing() const { return closing_; }        // 正在播收起动画
    bool   anim_running_now() const { return anim_running(now_ms()); }
    // 面板自己还在动（日历翻月的滚动、面板内淡入之类）：弹层要跟着出帧，
    // 不然展开动画一结束表就停了，控件内部的过渡全看不见。
    bool   panel_animating() const { return panel_ && panel_->animating(); }

    // ---- 自测/诊断用：读这一帧的像素（含 CPU 拼出来的动区）----
    uint64_t area_hash(int x, int y, int w, int h) const {
        if (art_cpu_.empty() || art_w_ <= 0) return 0;
        uint64_t hsh = 1469598103934665603ull;
        for (int row = 0; row < h; ++row) {
            int yy = y + row;
            if (yy < 0 || yy >= art_h_) continue;
            for (int col = 0; col < w; ++col) {
                int xx = x + col;
                if (xx < 0 || xx >= art_w_) continue;
                hsh ^= art_cpu_[(size_t)yy * (size_t)art_w_ + (size_t)xx];
                hsh *= 1099511628211ull;
            }
        }
        return hsh;
    }
    // 切月前后的两张动区截图是否真的不一样（不一样 = 确实换了月份）
    bool band_differs() const {
        if (band_old_.empty() || band_new_.empty()) return false;
        if (band_old_.size() != band_new_.size()) return true;   // 尺寸都不一样，肯定换了画面
        return std::memcmp(band_old_.data(), band_new_.data(),
                           band_new_.size() * sizeof(uint32_t)) != 0;
    }
    // 诊断：合成跑了几次 / 动区尺寸 / 两张截图大小
    int compose_count() const { return dbg_compose_n_; }
    void band_shape(int& w, int& h, int& old_sz, int& new_sz) const {
        w = band_w_; h = band_h_;
        old_sz = (int)band_old_.size(); new_sz = (int)band_new_.size();
    }
    void   tick_frame() { tick(); }   // 主循环调它推进一帧
    void invalidate_art() { art_dirty_ = true; }
    // 连出 n 帧，返回每帧平均毫秒（诊断用，绕开 WM_TIMER 的 15.6ms 节拍）
    double bench_render(int n) {
        double t0 = now_ms();
        for (int i = 0; i < n; ++i) render();
        return n > 0 ? (now_ms() - t0) / (double)n : -1.0;
    }

private:
    // 阴影留白：只在右/下，且只按面板真的需要多少算（不投影时是 0，窗口 = 面板，严丝合缝）
    int  pad_rb_ = 0;      // 右/下留白（像素）
    float pad_l_ = 0.f;    // 左/上留白（永远是 0，保留名字只为读起来清楚）

    // ---- 动画进度 ----
    // 展开：宽度一次给满，只有高度在长（顶边钉死在下拉触发点）。
    // 曲线是强 ease-out：头 70ms 冲掉一半以上行程，中段明显减速，末段留一段长缓冲
    // （最后 100ms 只走 1%），总 400ms。没有淡入、没有横向位移。
    // 收起：120ms 反向（起步慢、末段快），同样不做淡出。
    // 320ms：曲线换成 bezier(0.2,0.5,0.55,1)，后半段才看得见减速，太短会显得"一下就到位"
    static constexpr double kOpenMs = kPopupOpenMs;      // 展开：共用 core.hpp 的常数
    // 收起：100ms 在 240Hz 屏幕上只有 12 帧左右，而且 (1-t)^2 末段几乎不动，
    // 肉眼看就是"啪一下没了"。拉到 160ms 才看得出是从下往上被裁掉。
    static constexpr double kCloseMs = 160.0;
    static constexpr UINT   kFrameMs = 8;      // 动画出帧间隔（125 fps）
    // 三次贝塞尔缓动已提到 core.hpp（ease_bezier / ease_popup_open / ease_popup_close），
    // 块装盒与弹层共用同一组，保证同一屏里两处开合节奏一致。

    float anim_e(double now) const {
        if (e_override >= 0.f) return e_override;          // 离屏连拍用：给定进度直接出图
        float t = (float)((now - t0_) / (closing_ ? kCloseMs : kOpenMs));
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        if (closing_) return ease_popup_close(t);          // 收起：起步慢、末段快
        // 展开：起步一口气冲起来（起点斜率 2.5），然后**一路连续减速**到 0，
        // 每一帧都在动、每一帧都比上一帧慢 —— 中段看得见减速，末尾是一段长缓冲。
        return ease_popup_open(t);
    }
    bool anim_running(double now) const {
        if (e_override >= 0.f) return false;
        return closing_ ? (now - t0_) < kCloseMs : (now - t0_) < kOpenMs;
    }
    // 展开/收起的可见窗口。所有方向都是"竖向拉帘"——宽度从不参与动画：
    //   Down/Left/Right：顶边钉住，底边往下长；
    //   Up：底边钉住，顶边往上长。
    static Rect reveal_win(const Rect& pr, PopupDir d, float e) {
        if (d == PopupDir::Up) return Rect(pr.x, pr.y + pr.h * (1.f - e), pr.w, pr.h * e);
        return Rect(pr.x, pr.y, pr.w, pr.h * e);
    }
    // 内容贴着"正在动的那条边"：下拉时贴底边（先看到列表最后几项），
    // 上拉时贴顶边（先看到列表最前几项）—— 跟参考 GIF 的观感一致。
    static float reveal_shift(const Rect& pr, PopupDir d, float e) {
        float dy = pr.h * (1.f - e);
        return d == PopupDir::Up ? dy : -dy;
    }

    // ---- 窗口 ----
    bool create() {
        if (hwnd_) return true;
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = &PopupWin::WndProcStatic;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = cursor_arrow();
            wc.hbrBackground = nullptr;
            wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
            wc.hIconSm = wc.hIcon;
            wc.lpszClassName = L"w11ui_PopupClass";
            if (!RegisterClassExW(&wc)) return false;
            registered = true;
        }
        // 标题不能留空：空标题 + 无边框 + 分层 + 1×1 起步，是弹窗拦截器眼里标准的广告窗特征。
        // 跟着宿主窗口的标题走（宿主没标题就留空，库里不编一个出来）。
        WCHAR owner_title[256] = {};
        if (owner_) GetWindowTextW(owner_, owner_title, 256);
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                L"w11ui_PopupClass", owner_title, WS_POPUP, 0, 0, 1, 1,
                                owner_, nullptr, GetModuleHandleW(nullptr), this);
        return hwnd_ != nullptr;
    }

    // ---- 分层表面 ----
    bool ensure_surface(int px_w, int px_h) {
        if (rt_ && px_w == surf_w_ && px_h == surf_h_) return true;
        release_surface();
        HDC screen = GetDC(nullptr);
        mem_dc_ = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        if (!mem_dc_) return false;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = px_w;
        bi.bmiHeader.biHeight = -px_h;      // 负高度 = top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        hbmp_ = CreateDIBSection(mem_dc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbmp_) return false;
        old_bmp_ = SelectObject(mem_dc_, hbmp_);

        D2D1_RENDER_TARGET_PROPERTIES props{};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM,
                                             D2D1_ALPHA_MODE_PREMULTIPLIED};
        props.dpiX = props.dpiY = 96.f * scale_;
        props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        if (FAILED(d2d_factory()->CreateDCRenderTarget(&props, rt_.put()))) return false;
        RECT rc{0, 0, px_w, px_h};
        rt_->BindDC(mem_dc_, &rc);

        painter_ = Painter{};
        // premultiply=true：分层窗口的 DIB 是预乘 alpha，半透明阴影必须按预乘写
        painter_.init(rt_.get(), pal_, scale_, true, true);
        surf_w_ = px_w;
        surf_h_ = px_h;
        bits_ = bits;
        return true;
    }

    void release_surface() {
        painter_ = Painter{};
        art_.reset();          // 位图属于旧渲染目标，必须一起丢
        art_cpu_.clear();
        art_w_ = art_h_ = 0;
        art_dirty_ = true;
        need_full_ulw_ = true;
        rt_.reset();
        if (mem_dc_) {
            if (old_bmp_) { SelectObject(mem_dc_, old_bmp_); old_bmp_ = nullptr; }
            if (hbmp_) { DeleteObject(hbmp_); hbmp_ = nullptr; }
            DeleteDC(mem_dc_);
            mem_dc_ = nullptr;
        }
        surf_w_ = surf_h_ = 0;
        bits_ = nullptr;
    }

    void render() {
        if (!hwnd_ || !panel_) return;
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        int px_w = wr.right - wr.left, px_h = wr.bottom - wr.top;
        if (px_w <= 0 || px_h <= 0) return;
        if (!ensure_surface(px_w, px_h)) return;

        float e = anim_e(now_ms());
        double t_draw = now_ms();
        painter_.set_palette(pal_);
        painter_.set_scale(scale_);

        // 面板在弹层坐标系里的位置：左上角永远 (0,0)，右/下才留阴影的那一点点
        float pad_l = 0.f;
        Rect pr(pad_l, pad_l, panel_px_.w / scale_, panel_px_.h / scale_);

        // 面板内容是静态的（动画只裁一边、不重排），所以整块先渲成一张位图。
        // 帧循环里只做"贴位图 + 裁一条"，不再每帧重建文字布局/重画 16 层阴影——
        // 这也是原来 400ms 只出 18 帧、每帧 22~42ms 的主要开销。
        // 面板自己在动、底板已经建好 → 在 CPU 里拼它声明的那一小块（日历的日期区）。
        // 整块重建要重画 16 层阴影 + 重排全部文字，一帧 22~42 ms，那才是"卡卡的"来源。
        Rect motion{};
        bool has_motion = panel_ && panel_->motion_area(motion);
        if (art_dirty_ || !art_ || art_w_ != px_w || art_h_ != px_h) {
            // 截"重建之前"那一帧当旧内容（此刻 bits_ 还是上一帧的画面），再重建，最后截新的
            if (has_motion) cache_band(band_old_, band_old_w_, band_old_h_, motion, px_w, px_h);
            panel_->arrange(painter_, pr);   // 布局永远按最终矩形摆，命中测试不受动画影响
            rebuild_art(px_w, px_h);
            if (has_motion) cache_band(band_new_, band_w_, band_h_, motion, px_w, px_h);
        } else if (has_motion && panel_->animating()) {
            compose_motion_band(px_w, px_h);
        }

        // 每帧不再走 D2D：直接把像素写进分层窗口的 DIB（begin/end 的 Clear+Flush 实测要 4~20ms，
        // 帧节奏全被它拖垮）。D2D 只在 rebuild_art() 里用一次。
        double t1 = now_ms();
        if (e > 0.001f && !art_cpu_.empty() && bits_) {
            // 可见窗口 = 从锚边长出来的那一截；裁剪区比它大一圈（右/下）阴影留白，
            // 否则展开到 100% 时阴影刚好被裁掉
            Rect win = reveal_win(pr, dir_, e);
            int band0 = (int)std::lround(win.y * scale_);
            int band1 = (int)std::lround((win.y + win.h) * scale_) + pad_rb_;
            if (band0 < 0) band0 = 0;
            if (band1 > px_h) band1 = px_h;
            // 内容贴着"正在生长的那条边"：下拉时贴底边（先看到列表尾巴），上拉时贴顶边。
            // 对齐到整像素，逐行 memcpy 就是 NEAREST，不会把文字糊掉。
            int dy = (int)std::nearbyint(reveal_shift(pr, dir_, e) * scale_);
            uint32_t* dst = (uint32_t*)bits_;
            std::memset(dst, 0, (size_t)px_w * (size_t)px_h * 4);
            for (int y = band0; y < band1; ++y) {
                int sy = y - dy;
                if (sy >= 0 && sy < art_h_)
                    std::memcpy(dst + (size_t)y * px_w, art_cpu_.data() + (size_t)sy * px_w,
                                (size_t)px_w * 4);
            }
        } else if (bits_) {
            std::memset(bits_, 0, (size_t)px_w * (size_t)px_h * 4);
        }
        double t2 = now_ms();
        double t3 = t2;
        dbg_begin_ms_ = t1 - t_draw;
        dbg_blit_ms_  = t2 - t1;
        dbg_end_ms_   = t3 - t2;
        dbg_draw_ms_ = now_ms() - t_draw;

        // 整块上传。实测过"只上传正在露出的那一条"，反而更慢（75ms 一帧）：
        // 部分区域的 UpdateLayeredWindow 会让 DWM 重算整窗，别这么优化。
        double t_ulw = now_ms();
        HDC screen = GetDC(nullptr);
        POINT dst{wr.left, wr.top};
        SIZE sz{px_w, px_h};
        POINT src{0, 0};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd_, screen, &dst, &sz, mem_dc_, &src, 0, &bf, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
        need_full_ulw_ = false;
        dbg_ulw_ms_ = now_ms() - t_ulw;
        last_frame_ = now_ms();
        if (frames_.size() < 400) frames_.push_back(now_ms());   // 帧间隔诊断用
    }

    // 把整块面板（100% 展开状态）渲到分层表面，再取成一张 D2D 位图留着复用
    void rebuild_art(int px_w, int px_h) {
        painter_.begin(Color(0.f, 0.f, 0.f, 0.f));
        panel_->paint(painter_);
        painter_.end();
        Com<ID2D1Bitmap> bmp;
        D2D1_BITMAP_PROPERTIES bp{};
        bp.pixelFormat =
            D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        bp.dpiX = bp.dpiY = 96.f * scale_;
        // 注意：这份 MinGW 头是 Direct2D 1.0，CreateBitmap 没有 bitmapOptions 参数
        if (FAILED(rt_->CreateBitmap(D2D1::SizeU((UINT32)px_w, (UINT32)px_h), bits_,
                                     (UINT32)px_w * 4, &bp, bmp.put())))
            return;
        art_ = bmp;
        art_w_ = px_w;
        art_h_ = px_h;
        // 再留一份 CPU 侧的整块像素：每帧只用 memcpy 竖着切一条，
        // 不走 D2D 的 EndDraw（实测一次全窗 alpha 合成要 8~20ms，帧节奏全被它拖垮）
        art_cpu_.resize((size_t)px_w * (size_t)px_h);
        if (bits_) std::memcpy(art_cpu_.data(), bits_, (size_t)px_w * px_h * 4);
        art_dirty_ = false;
    }

    // ---- 动区（比如日历的日期区）的逐帧合成 ----
    // 整块重建**之前**把当前画面这块截一张存成"旧内容"，重建**之后**再截一张存成"新内容"；
    // 之后每帧只在 CPU 里按进度把两者的行拼起来，不进 D2D、不重排文字 —— 一帧 0.1 ms 级。
    void cache_band(std::vector<uint32_t>& out, int& ow, int& oh, const Rect& r, int px_w, int px_h) {
        out.clear(); ow = oh = 0;
        if (!bits_) return;
        int x = (int)std::lround(r.x * scale_);
        int y = (int)std::lround(r.y * scale_);
        int w = (int)std::lround(r.w * scale_);
        int h = (int)std::lround(r.h * scale_);
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > px_w) w = px_w - x;
        if (y + h > px_h) h = px_h - y;
        if (w <= 0 || h <= 0) return;

        ow = w; oh = h;
        out.resize((size_t)w * (size_t)h);
        const uint32_t* src = (const uint32_t*)bits_;
        for (int row = 0; row < h; ++row)
            std::memcpy(out.data() + (size_t)row * (size_t)w,
                        src + (size_t)(y + row) * (size_t)px_w + (size_t)x,
                        (size_t)w * 4);
    }

    void compose_motion_band(int px_w, int px_h) {
        ++dbg_compose_n_;
        if (art_cpu_.empty() || band_new_.empty() || band_w_ <= 0 || band_h_ <= 0) return;
        if (!panel_) return;
        float e = panel_->motion_e();
        int   dir = panel_->motion_dir();
        if (e < 0.f) e = 0.f;
        if (e > 1.f) e = 1.f;

        bool has_old = !band_old_.empty() && band_old_w_ > 0 && band_old_h_ > 0;
        uint32_t* dst = art_cpu_.data();
        int shift = (int)std::lround(e * (float)band_h_);
        for (int t = 0; t < band_h_; ++t) {
            int row_old, row_new;
            if (dir >= 0) {          // 往后翻：旧内容往上走，新内容从下面进
                row_old = t + shift;
                row_new = t - (band_h_ - shift);
            } else {                 // 往前翻：反过来
                row_old = t - shift;
                row_new = t + (band_h_ - shift);
            }
            uint32_t* pdst = dst + (size_t)(band_y_ + t) * (size_t)px_w + (size_t)band_x_;
            if (has_old && row_old >= 0 && row_old < band_old_h_) {
                // 旧图宽度可能差一两个像素（两次 arrange 的浮点取整），按较窄的拷，右边用新图补
                int cw = (band_old_w_ < band_w_) ? band_old_w_ : band_w_;
                std::memcpy(pdst, band_old_.data() + (size_t)row_old * (size_t)band_old_w_,
                            (size_t)cw * 4);
                if (cw < band_w_) {
                    int rn2 = (row_new >= 0 && row_new < band_h_) ? row_new : row_old;
                    std::memcpy(pdst + cw, band_new_.data() + (size_t)rn2 * (size_t)band_w_ + cw,
                                (size_t)(band_w_ - cw) * 4);
                }
            } else if (row_new >= 0 && row_new < band_h_) {
                std::memcpy(pdst, band_new_.data() + (size_t)row_new * (size_t)band_w_,
                            (size_t)band_w_ * 4);
            }
        }
    }

    // 动画出帧时钟：8ms 一帧（≈125 fps）+ 1ms 计时器精度。
    // 原来 15ms/66fps 时会连着漏拍——实测同一段动画有的帧走 15ms、有的走 31ms，
    // 一帧能蹦 58 像素、下一帧只蹦 1 像素，看着就是"卡一下、抖一下"，
    // 曲线再怎么调也被帧节奏吃掉。timeBeginPeriod(1) 让 Sleep/SetTimer 真的按 1ms 走。
    void start_anim_clock() {
        if (!hwnd_) return;
        if (!clock_on_) {
            timeBeginPeriod(1);
            clock_on_ = true;
        }
        SetTimer(hwnd_, 1, kFrameMs, nullptr);
    }
    void stop_anim_clock() {
        if (hwnd_) KillTimer(hwnd_, 1);
        if (clock_on_) {
            timeEndPeriod(1);
            clock_on_ = false;
        }
    }

    void tick() {
        if (!shown_) return;
        double now = now_ms();
        bool pane_anim = panel_animating();
        if (anim_running(now) || pane_anim) {
            // 只在"到点"才出帧：主循环会 1ms 轮询着调过来，这里闸门保证 8ms 一帧，
            // 不会因为消息频繁就狂渲
            if (now - last_frame_ >= (double)kFrameMs) {
                render();
            }
            return;
        }
        if (closing_) {
            // 收完了：真正藏起来并通知控件
            bool had = shown_;
            shown_ = false;
            closing_ = false;
            if (hwnd_) {
                stop_anim_clock();
                ShowWindow(hwnd_, SW_HIDE);
            }
            auto p = panel_;
            panel_.reset();
            if (had && p && p->on_cancel) p->on_cancel();
            return;
        }
        stop_anim_clock();   // 展开完成后停表，不空转
    }

    Point to_logical(LPARAM lp) const {
        // 注意：这里只能除缩放，不能再减留白。
        // 面板是在 render() 里按 arrange(Rect(pad_l, pad_l, w, h)) 摆的，它的 bounds 原点
        // 本来就在留白处（不投影时留白 = 0）；再减一次留白，命中区就会整体比画出来的行高
        // 一个留白（以前写死 14px 时，看起来正好是"悬浮检测区向下偏了 12px"，点最后一行直接落空）
        const float s = scale_ > 0.f ? scale_ : 1.f;
        return Point((float)GET_X_LPARAM(lp) / s, (float)GET_Y_LPARAM(lp) / s);
    }

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
#ifdef W11UI_POPUP_TRACE
        if (msg != WM_TIMER && msg != WM_PAINT && msg != 0x0006 /*WM_ACTIVATE*/ && msg != 0x0083 /*WM_NCCALCSIZE*/) {
            std::fprintf(stderr, "[popup] msg=%u pt=(%d,%d) shown=%d closing=%d panel=%d\n",
                         msg, (int)GET_X_LPARAM(lp), (int)GET_Y_LPARAM(lp),
                         (int)shown_, (int)closing_, (int)(panel_ ? 1 : 0));
            std::fflush(stderr);
        }
#endif
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd_, &ps);
                EndPaint(hwnd_, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;

            case WM_MOUSEMOVE: {
                if (panel_) panel_->mouse_move(to_logical(lp));
                if (!headless_) {   // 自测里真实指针不在弹层上，跟踪会马上收到 MOUSELEAVE
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                    TrackMouseEvent(&tme);
                }
                // 停在带 ▸ 的行上：起一个 16ms 的计时器，到点自动展开子菜单
                if (panel_ && panel_->hover_waiting()) SetTimer(hwnd_, 2, 16, nullptr);
                else KillTimer(hwnd_, 2);
                return 0;
            }
            case WM_MOUSELEAVE:
                if (panel_) panel_->mouse_leave();
                KillTimer(hwnd_, 2);
                return 0;

            case WM_NCHITTEST:
                // 客户区就是内容：别让 DefWindowProc 把弹层当成标题栏去拖动，
                // 一旦进拖动循环，WM_LBUTTONUP 就永远到不了下面那个分支，点行就不生效了。
                return HTCLIENT;

            case WM_LBUTTONDOWN: {
                // 按下就把鼠标抓住：手指抖一下松在面板外，UP 也要回到这一层，
                // 不能掉到主窗口去——主窗口一看"有弹层"就把整栈收掉，值也就没了。
                SetCapture(hwnd_);
                press_pt_ = to_logical(lp);
                if (panel_) panel_->mouse_down(press_pt_);   // 取色器靠这个起"按住拖"
                return 0;
            }

            case WM_LBUTTONUP:
                if (panel_ && !closing_) {
                    auto p = panel_;
                    Point pt = to_logical(lp);
                    p->hover = -1;
                    p->click(pt);
                }
                if (GetCapture() == hwnd_) ReleaseCapture();
                return 0;

            case WM_TIMER:
                if (wp == 2) {
                    // 悬停到点：把这一行的子菜单拉出来（只触发一次；已经开着的那一行不再重开）
                    if (panel_) {
                        auto p = panel_;
                        int row = p->tick_hover(now_ms());
                        if (row >= 0) {
                            KillTimer(hwnd_, 2);
                            if (!(p->open_child_row == row && p->child_open && p->child_open())) {
                                if (p->host) p->host->note_submenu_source(p->popup_level);
                                if (p->on_submenu) p->on_submenu(row);
                            }
                        }
                    }
                    return 0;
                }
                tick();
                return 0;

            case WM_MOUSEACTIVATE:
                // 点弹层绝不能抢主窗口的激活，否则主窗口立刻收到 WA_INACTIVE，
                // 它会以为"用户点到别处了"把弹层收掉 —— 这一层被销毁，后面的 UP 就无处可去。
                return MA_NOACTIVATE;

            case WM_ACTIVATE:
                return 0;

            default:
                break;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        PopupWin* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = (CREATESTRUCTW*)lp;
            self = (PopupWin*)cs->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
            if (self) self->hwnd_ = h;
        } else {
            self = (PopupWin*)GetWindowLongPtrW(h, GWLP_USERDATA);
        }
        if (self) return self->handle(msg, wp, lp);
        return DefWindowProcW(h, msg, wp, lp);
    }

    HWND  hwnd_ = nullptr;
    HWND  owner_ = nullptr;
    bool  shown_ = false;
    bool  clock_on_ = false;   // timeBeginPeriod(1) 是否已开
    bool  closing_ = false;
    bool  headless_ = false;
    bool  need_full_ulw_ = true;   // 下一帧要不要整块上传（首帧/重定位/换主题后）
    std::vector<double> frames_;   // 出帧时间戳，诊断帧间隔
    std::vector<uint32_t> art_cpu_; // 整块面板像素（CPU 侧），每帧靠它逐行 memcpy

    // 动区（日历日期区）的两次截图：band_old_ = 切月前那一帧，band_new_ = 切月后那一帧。
    // 滚动每帧只用它们拼行，像素坐标（相对窗口）。
    std::vector<uint32_t> band_old_, band_new_;
    int band_x_ = 0, band_y_ = 0, band_w_ = 0, band_h_ = 0;          // band_new_ 的矩形（像素）
    int band_old_w_ = 0, band_old_h_ = 0;                            // band_old_ 自己的宽高
    int dbg_compose_n_ = 0;     // 合成跑了几次（诊断）
    double last_frame_ = 0.0;      // 上一帧的时间（帧闸门）
    double dbg_draw_ms_ = 0.0;     // 上一帧"画"的耗时（D2D 贴图）
    double dbg_begin_ms_ = 0.0;    // 其中：begin（清底）
    double dbg_blit_ms_  = 0.0;    // 其中：DrawBitmap + 裁剪
    double dbg_end_ms_   = 0.0;    // 其中：EndDraw
    double dbg_ulw_ms_  = 0.0;     // 上一帧 UpdateLayeredWindow 的耗时
    double t0_ = 0.0;
    float  e_override = -1.f;   // >=0 时锁定展开进度，只给离屏连拍用
    PopupDir dir_ = PopupDir::Down;
    Palette pal_;
    float scale_ = 1.f;
    Rect  panel_px_;
    Rect  panel_logical_;   // 客户区逻辑坐标，给 save_png 叠图用
    Point press_pt_{};      // 按下时的客户区逻辑坐标（诊断/兜底用）

    std::shared_ptr<PopupPanel> panel_;
    Painter painter_;

    HDC     mem_dc_ = nullptr;
    HBITMAP hbmp_ = nullptr;
    HGDIOBJ old_bmp_ = nullptr;
    Com<ID2D1DCRenderTarget> rt_;
    int surf_w_ = 0, surf_h_ = 0;
    void* bits_ = nullptr;

    // 整块面板的预渲染位图：动画帧只贴它、裁一刀，不再重画文字和阴影
    Com<ID2D1Bitmap> art_;
    int  art_w_ = 0, art_h_ = 0;
    bool art_dirty_ = true;
};

// ---------------------------------------------------------------- Scene ----
class Scene : public Host {
public:
    std::shared_ptr<Stack> root;
    Palette palette = make_palette(false);
    bool  dark = false;
    float scale = 1.f;
    Window* window = nullptr;

    std::map<std::string, std::function<void(Event&)>> handlers;
    std::function<void()>  on_repaint;
    std::function<void(Point)> on_move;   // 供窗口更新光标

    bool    need_frames = false;   // 本帧有控件还在动画中
    Widget* hot = nullptr;
    Widget* press = nullptr;
    Widget* focus = nullptr;                      // 键盘焦点（输入框）
    std::shared_ptr<Widget> modal;                // 模态层（Dialog），整屏吃事件
    Point   last_pt;
    Size    client_size{800.f, 600.f};   // 逻辑像素的客户区大小

    void layout(Painter& p, Size logical) {
        client_size = logical;
        if (root) {
            root->attach(this);
            root->arrange(p, Rect(0.f, 0.f, logical.w, logical.h));
        }
        if (modal) {
            modal->attach(this);
            modal->arrange(p, Rect(0.f, 0.f, logical.w, logical.h));
        }
    }

    void paint(Painter& p) {
        need_frames = false;
        if (root) root->paint(p);
        if (modal) {
            modal->paint(p);
            // 关闭动画最后一帧画完再摘掉模态层：销毁放在这里（画完之后），不会 delete this
            sweep_modal();
        }
    }

    bool animating() const { return need_frames; }

    // ---- 焦点 ----
    void set_focus(Widget* w) {
        if (focus == w) {
            // 同一控件再点一次：焦点没变，但"点它"这件事发生了 —— 输入框要重新开始闪、
            // 挂了数字键盘的框要重新弹层（点确定/取消关掉以后再点同一个框就是这条路）。
            if (w) w->on_focus(true);
            request_repaint();
            return;
        }
        if (focus) focus->on_focus(false);
        focus = w;
        if (focus) focus->on_focus(true);
        request_repaint();
    }
    void want_focus(Widget* w) override { set_focus(w); }

    // ---- 模态层 ----
    void open_modal(std::shared_ptr<Widget> layer) override {
        modal = std::move(layer);
        if (modal) {
            modal->attach(this);
            if (on_repaint) on_repaint();
        }
    }
    void close_modal() override {
        if (!modal) return;
        // 顺序很重要：焦点控件多半是模态层里的（弹窗里的输入框），
        // 先交还焦点（这时它还在世），再销毁模态层，否则 set_focus 会写已经析构的对象
        set_focus(nullptr);
        modal.reset();
        if (on_repaint) on_repaint();
    }
    bool modal_open() const { return (bool)modal; }
    // 回调里（Dialog 按钮）只标记 finished，销毁留到本次事件末尾，避免 delete this
    void sweep_modal() {
        if (!modal) return;
        auto* d = dynamic_cast<Dialog*>(modal.get());
        if (d && d->finished()) close_modal();
    }

    // ---- 键盘 ----
    bool key_down(int vk) {
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool dealt = false;
        if (modal) dealt = modal->on_key(vk, ctrl, shift);
        if (!dealt && focus) dealt = focus->on_key(vk, ctrl, shift);
        sweep_modal();
        return dealt;
    }
    bool char_input(wchar_t ch) {
        if (!focus || !focus->wants_key()) return false;
        focus->on_char(ch);
        sweep_modal();
        return true;
    }

    // ---- Host 接口 ----
    void request_repaint() override { if (on_repaint) on_repaint(); }
    void mark_animating() override { need_frames = true; }
    void fire(const std::string& name, Widget* w, bool checked) override {
        auto it = handlers.find(name);
        if (it == handlers.end() || !it->second) return;
        Event e;
        e.name = name;
        e.widget = w;
        e.window = window;
        e.checked = checked;
        it->second(e);
    }
    void want_hand_cursor(bool) override {}

    // ---- 弹出层支撑 ----
    Rect client_rect() const override { return Rect(0.f, 0.f, client_size.w, client_size.h); }
    float dpi_scale() const override { return scale > 0.f ? scale : 1.f; }

    // 下面这几个要用到完整的 Window（本类定义在 Window 之前），实现放在文件末尾
    Rect client_screen_rect() const override;
    Rect monitor_work_rect() const override;
    bool show_popup(std::shared_ptr<PopupPanel> panel, const Rect& screen_px, PopupDir dir) override;
    bool open_submenu(std::shared_ptr<PopupPanel> panel, int row) override;
    // 面板请求拉出子菜单前先报"我是第几层"，Scene::open_submenu 就按这一层往右开，
    // 而不是一律从"当前最深一层"往右开（那样悬浮+点击会一层层往上堆同样的面板）。
    void note_submenu_source(int level) override { submenu_src_level_ = level; }
    // 取出并清掉（一次性，避免污染下一次不经过面板的调用）
    int take_submenu_source() {
        int v = submenu_src_level_;
        submenu_src_level_ = -1;
        return v;
    }
    void close_popup() override;
    bool popup_visible() const override;
    void set_popup_hover(bool on, double delay_ms) override;   // 实现在文件末尾（要完整的 Window）

    int submenu_src_level_ = -1;   // 最近一次"哪一层的面板要点子菜单"

    // ---- 鼠标 ----
    // 有模态层时全屏都归它，下面的控件一个事件也收不到
    Widget* target(Point pt) {
        if (modal) return modal->hit_test(pt);
        return root ? root->hit_test(pt) : nullptr;
    }
    void mouse_move(Point pt) {
        last_pt = pt;
        Widget* hit = target(pt);
        if (hit != hot) {
            if (hot) hot->on_hover(false);
            hot = hit;
            if (hot) hot->on_hover(true);
            if (on_move) on_move(pt);
        }
        if (hot) hot->on_move(pt);
        if (press) press->on_drag(pt);
    }
    void mouse_down(Point pt) {
        mouse_move(pt);
        // 点到别的控件上就交还焦点（点输入框自己会重新拿回来）
        if (focus && hot != focus) set_focus(nullptr);
        press = hot;
        if (press) press->on_press(true);
    }
    void mouse_up(Point pt) {
        mouse_move(pt);
        Widget* w = press;
        press = nullptr;
        if (!w) return;
        w->on_press(false);
        // 按下与抬起落在同一个控件才算点击
        if (target(pt) == w) w->on_click_at(pt);
        sweep_modal();
    }
    void mouse_leave() {
        if (hot) hot->on_hover(false);
        hot = nullptr;
        if (press) { press->on_press(false); press = nullptr; }
    }
};

// --------------------------------------------------------------- Window ----
class Window {
public:
    // 窗口外壳：默认就是系统那条操作栏，客户区纯净实色（库原本的观感，什么都不变）。
    // Custom 才把系统栏整条去掉、换成库自己画的标题栏（按钮/拖动/双击最大化都由库接管）。
    enum class Chrome { System, Custom };
    // 背景材质。None = 实色（默认，不压暗）；Acrylic / Mica = 亚克力 / 云母，
    // 能不能真的透出来由系统和渲染后端决定，set_backdrop() 的返回值就是实测结果。
    enum class Backdrop { None, Acrylic, Mica };

    template <class... Args>
    explicit Window(Args&&... args) {
        Opts o;
        apply_opts(o, std::forward<Args>(args)...);
        title_ = o.title;   // 标题栏文字由调用方给：库不写死默认标题（空 = 不显示标题）
        width_ = or_default(o.width, 960.f);
        height_ = or_default(o.height, 640.f);
        theme_ = o.theme;
        accent_ = o.accent;
        padding_ = or_default(o.padding, 24.f);
        spacing_ = or_default(o.spacing, 14.f);
        max_width_ = o.max_width;

        scene_.window = this;
        scene_.on_repaint = [this]() { invalidate(); };
        scene_.on_move = [this](Point) { refresh_cursor(); };
        scene_.root = std::make_shared<Stack>(kw::spacing(spacing_), kw::padding(padding_),
                                              kw::max_width(max_width_));
        // 窗口级点击：没命中任何控件时，空白处点击也派发事件（对应 Python 的 tickthing="a"）
        if (!o.click.empty()) scene_.root->click_name = o.click;

        rebuild_palette();
    }

    ~Window() {
        for (auto& p : popups_) if (p) p->destroy();
        popups_.clear();
        if (hwnd_) DestroyWindow(hwnd_);
    }

    Scene& scene() { return scene_; }
    Window& add(std::shared_ptr<Widget> w) {
        scene_.root->add(std::move(w));
        return *this;
    }
    Window& on(const std::string& name, std::function<void(Event&)> fn) {
        scene_.handlers[name] = std::move(fn);
        return *this;
    }
    // 弹出模态弹窗。返回的 shared_ptr 归窗口接（Scene 里的模态层），回调结束后自动销毁
    template <class... Args>
    std::shared_ptr<Dialog> dialog(Args&&... args) {
        auto d = std::make_shared<Dialog>(std::forward<Args>(args)...);
        scene_.open_modal(d);
        return d;
    }
    // 手动挂任意模态层（Dialog 之外的自定义全屏层）
    Window& modal(std::shared_ptr<Widget> layer) {
        scene_.open_modal(std::move(layer));
        return *this;
    }
    void close_dialog() { scene_.close_modal(); }
    // 首帧画完（控件已完成布局）回调一次：想一启动就展开下拉框、或做自动演示，用这个
    Window& on_ready(std::function<void()> fn) {
        on_ready_ = std::move(fn);
        return *this;
    }
    // 窗口已显示、DPI 和材质都落定后回调一次（run() 里，首帧之前）。
    // 启动就要弹浮层的程序挂这里：run() 之前窗口还没建，那时 show_popup 拿不到 owner。
    Window& on_first_show(std::function<void()> fn) {
        on_shown_ = std::move(fn);
        return *this;
    }
    // 把数字键盘挂到某个输入框上：该框聚焦时在它下方弹出（带展开动画），弹层自己的
    // 点外面关 / Esc / 确定 / 取消 都照常走。程序不调这一句，就完全没有这个行为。
    bool attach_numpad(std::shared_ptr<TextBox> box, std::shared_ptr<NumberPad> pad) {
        if (!box || !pad) return false;
        pad->target = box;
        box->numpad_on_focus = true;
        auto prev = box->on_focus_change;
        auto wp = std::weak_ptr<NumberPad>(pad);
        auto wb = std::weak_ptr<TextBox>(box);
        // 确定 / 取消必须收掉这层弹层，否则点了等于没点。挂在 auto_close 上而不是包
        // on_ok：程序在 attach 之后再设 on_ok 也不会把"关门"这一步弄丢。
        // 整数框：小数点键直接灰掉不响应，和 TextBox 挡字符那一层对上。
        pad->auto_close = [this]() { close_popup(); };
        box->on_focus_change = [this, prev, wp, wb](bool on) {
            if (prev) prev(on);
            if (!on) return;                       // 失焦不自动收：点键盘本体也会让窗口失活，收了就没法用
            auto pad = wp.lock();
            auto b = wb.lock();
            if (!pad || !b) return;
            pad->target = b;                        // 同一个键盘挂多个框时，键要写进"当前这个"框
            pad->dot_enabled = !b->num_integer;      // 整数框不许按小数点
            if (popup_open()) return;                // 真的有一层开着：别叠第二层
            if (popup_visible()) close_popup_now();  // 只剩正在收的旧层：立刻清掉，换新的弹
            float s = scene_.dpi_scale();
            Rect cs = scene_.client_screen_rect();
            float x = cs.x + b->bounds.x * s;
            float y = cs.y + b->bounds.bottom() * s + 4.f * s;
            show_popup(pad, Rect(x, y, pad->preferred_width() * s, pad->preferred_height() * s),
                       PopupDir::Down);
        };
        return true;
    }
    Window& title(const std::wstring& t) {
        title_ = t;
        if (hwnd_) SetWindowTextW(hwnd_, t.c_str());
        return *this;
    }

    // 主题：Auto 跟随系统，Light/Dark 强制。改完立即重绘（含标题栏 + 正开着的菜单）
    void set_theme(Theme t) {
        theme_ = t;
        rebuild_palette();
        // 已经弹出来的菜单/下拉框也要立刻换色（Edge 的菜单就是深/浅两套跟着走）
        for (auto& p : popups_) if (p) p->set_palette(scene_.palette);
        apply_dwm();
        if (backdrop_ != Backdrop::None) apply_backdrop();   // 材质的深浅取向跟着主题走
        invalidate();
    }
    Theme theme() const { return theme_; }
    bool  is_dark() const { return scene_.dark; }
    static int build_number() { return os_build(); }   // 22000+ 才算 Win11

    // 程序把键盘焦点交给某个控件（等价于用户点了它）：输入框会开始闪光标，
    // 挂了数字键盘的那个框会照常弹层。手写 widget->on_focus(true) 不算数 ——
    // 那样 Scene 不知道焦点在谁身上，光标闪烁和"点空白取消"都会失效。
    void focus(Widget* w) {
        scene_.set_focus(w);
        invalidate();
    }

    void invalidate() {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // 有没有弹层正在播动画（主循环据此切成"1ms 轮询出帧"模式）
    bool popups_animating() const {
        double now = now_ms();
        for (auto& p : popups_)
            if (p && p->shown_for_anim() && (p->anim_running_now() || p->panel_animating())) return true;
        return false;
    }
    // 主循环每轮调一次：到点才出帧；动画收尾也在里面
    void tick_popups() {
        for (size_t i = 0; i < popups_.size(); ++i)
            if (popups_[i]) popups_[i]->tick_frame();
        // 已经收完的层要从栈里摘掉：否则 popup_depth 一直带着死层，
        // 而且每次弹新层都会在数组尾巴上堆一个空壳。
        while (!popups_.empty() && popups_.back() && !popups_.back()->visible()) {
            popups_.back()->destroy();
            popups_.pop_back();
        }
    }

    int run() {
        if (!create()) return -1;
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
        apply_dwm();            // 窗口可见后再落一次 DWM 属性，隐藏时设的有时会被忽略
        apply_backdrop();       // 材质同理：显示之后再应用才稳定
        // 首帧前的一次性回调：程序要"启动就弹个浮层"，就得等窗口和 DPI 都定型之后
        // （run() 之前调 show_popup 会因为 owner 还没 hwnd 而失败）
        if (on_shown_) { auto f = std::move(on_shown_); on_shown_ = nullptr; f(); }
        MSG msg;
        // 有弹层动画时不再阻塞在 GetMessage：1ms 轮询 + 8ms 出帧。
        // 全靠 WM_TIMER 会连拍/漏拍（实测相邻帧 1.4ms / 39.6ms 交替），看着就是卡顿。
        for (;;) {
            if (popups_animating() || scene_.animating()) {
                MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, 0);
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                tick_popups();
                continue;
            }
            if (!GetMessageW(&msg, nullptr, 0, 0)) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return 0;
    }

    // 建窗口但不显示：自动化自测用，屏幕上什么都不出现（弹层也不会弹出来）
    bool create_hidden() {
        headless_ = true;
        if (!create()) return false;
        ShowWindow(hwnd_, SW_HIDE);
        return true;
    }

    void close() {
        if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }

    HWND hwnd() const { return hwnd_; }

    // ---- 窗口外壳 ----
    // 默认 Chrome::System：系统操作栏 + 客户区纯净实色，和库以前一模一样。
    // 换成 Chrome::Custom 之后：
    //   · 系统标题栏整条不画，客户区从窗口顶边开始；
    //   · 库画一条高 title_h 的标题栏：图标 + 标题文字（程序给）+ 最小化/最大化/关闭；
    //   · 按住标题栏空白可拖动、双击可最大化/还原，按钮和系统按钮行为一致；
    //   · 客户区内容整体下移 title_h，命中/鼠标坐标一起跟着移。
    // 按钮位置、hover 面、关闭红都是控件自己的装饰，标题文字永远来自程序。
    void set_chrome(Chrome c) {
        if (chrome_mode_ == c) return;
        // 切换前先把"内容区"尺寸记下来：换外壳不该让程序看到的画面变大变小
        float content_w = 0.f, content_h = 0.f;
        if (hwnd_) {
            RECT cr;
            GetClientRect(hwnd_, &cr);
            float old_th = (chrome_mode_ == Chrome::Custom) ? title_h_ : 0.f;
            content_w = (float)(cr.right - cr.left) / scale_;
            content_h = (float)(cr.bottom - cr.top) / scale_ - old_th;
        }
        chrome_mode_ = c;
        if (hwnd_) {
            apply_style();
            if (content_w > 0.f && content_h > 0.f) {
                int tw = (int)std::lround(content_w), thh = (int)std::lround(content_h);
                int ww, wh;
                if (c == Chrome::Custom) {
                    ww = tw;                       // 客户区盖满窗口：窗口尺寸 = 内容 + title_h
                    wh = thh + (int)title_h_;
                } else {
                    RECT rc{0, 0, tw, thh};
                    AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(hwnd_, GWL_STYLE), FALSE, 0);
                    ww = rc.right - rc.left;
                    wh = rc.bottom - rc.top;
                }
                SetWindowPos(hwnd_, nullptr, 0, 0, ww, wh,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            apply_dwm();
            invalidate();
        }
    }
    Chrome chrome() const { return chrome_mode_; }

    // 自绘标题栏高度（逻辑像素）。默认 32；改小到 24 会更紧凑，改大不影响其它东西。
    void set_title_height(float px) {
        title_h_ = px < 16.f ? 16.f : px;
        if (hwnd_ && chrome_mode_ == Chrome::Custom) { invalidate(); }
    }
    float title_height() const { return title_h_; }

    // 自绘标题栏上的文字。不设就用窗口标题（kw::title，也是程序传进来的）。
    // 传空字符串 = 只画图标、不画字（库不自带任何文案）。
    void set_title_text(const std::wstring& t) { title_text_ = t; invalidate(); }
    const std::wstring& title_text() const { return title_text_.empty() ? title_ : title_text_; }

    // 标题栏上要哪几个按钮：默认三个都画。有些小窗口不要最大化就 set_title_buttons(true,false,true)。
    void set_title_buttons(bool minb, bool maxb, bool closeb) {
        show_min_ = minb; show_max_ = maxb; show_close_ = closeb;
        invalidate();
    }

    // 背景材质：None（默认，实色、不压暗）/ Acrylic / Mica。
    // 返回 true 表示系统真的接了这个材质（Win11 22621+ 才有 Mica；Win10 只有亚克力，
    // 而且因为客户区是 D2D 直接画上去的不透明像素，材质只在系统栏/边框那一圈可见）。
    // 要在窗口显示之后再调一次才稳定（隐藏窗口时设材质系统经常忽略）。
    bool set_backdrop(Backdrop b, bool dark_tint = true) {
        backdrop_ = b;
        backdrop_dark_tint_ = dark_tint;
        backdrop_supported_ = apply_backdrop();
        invalidate();
        return backdrop_supported_;
    }
    Backdrop backdrop() const { return backdrop_; }
    bool backdrop_supported() const { return backdrop_supported_; }

    // 把自绘标题栏画到指定的 Painter 上（离屏出图 / 自己合成整窗图时用）
    void paint_title_bar(Painter& p, float w) { paint_chrome_into(p, w); }

    // 客户区底走磨砂面（"背景亚克力"的视觉近似）。默认 false = 纯实色。
    // 注意这和 set_backdrop 不是一回事：那个是把窗口交给 DWM 去画材质，只在系统栏那圈能看见；
    // 这个是本库自己在客户区铺一层上亮下暗的玻璃渐变，和控件是否磨砂无关，可各自独立开关。
    void set_acrylic(bool on = true) { surface_acrylic_ = on; invalidate(); }
    bool acrylic() const { return surface_acrylic_; }

    // ---- 弹出层（下拉框 / 菜单）----
    // popups_ 是个栈：第 0 层是下拉框自己，往上是"菜单里的子菜单"，可以一直套下去。
    bool show_popup(std::shared_ptr<PopupPanel> panel, const Rect& screen_px, PopupDir dir) {
        if (!hwnd_) return false;
        close_popup_now();
        popups_.clear();
        return spawn_popup(0, std::move(panel), screen_px, dir);
    }

    // ---- 日期选择器：**唤起是程序的事** ----
    //   app.pick_date(date_box, kw::format("yyyy/MM/dd"));   // 格式不给就是 yyyy-MM-dd
    // 面板贴在 target 下方（下面放不下就翻到上方），走的是同一套弹层窗口/展开动画，
    // 所以观感和下拉框、菜单一致。选完由库把日期按 format 写回 box->text，并派发
    // box 自己的 change 事件；程序给的 kw::on_pick_date 也会收到一份（清除时是 0,0,0）。
    template <class... Args>
    std::shared_ptr<DatePicker> pick_date(const std::shared_ptr<Widget>& target, Args&&... args) {
        if (!hwnd_) return nullptr;
        auto dp = std::make_shared<DatePicker>(std::forward<Args>(args)...);
        auto box = std::dynamic_pointer_cast<TextBox>(target);

        std::wstring pat = dp->date_format.empty() ? std::wstring(L"yyyy-MM-dd") : dp->date_format;
        // 程序没给初始日期：就把输入框里现在那串按同一个格式反解出来
        if (dp->sel_y == 0 && box) {
            int y = 0, m = 0, d = 0;
            if (parse_date(box->text, pat, y, m, d)) dp->set_date(y, m, d);
        }

        auto user_cb = dp->on_pick_date;
        dp->on_pick_date = [this, box, pat, user_cb](int y, int m, int d) {
            if (box) {
                box->set_text(y > 0 ? format_date(y, m, d, pat) : std::wstring());
                if (!box->change_name.empty()) scene_.fire(box->change_name, box.get(), true);
            }
            if (user_cb) user_cb(y, m, d);
            close_popup();
        };

        Rect cr = scene_.client_rect();
        Rect tb = target ? target->bounds : Rect(cr.x + 8.f, cr.y + 8.f, 1.f, 1.f);
        float w = dp->preferred_width();
        float h = dp->preferred_height();
        float gap = 6.f;
        bool down = (tb.bottom() + gap + h <= cr.bottom() + 0.5f);
        float y = down ? (tb.bottom() + gap) : (tb.y - gap - h);
        if (y < cr.y) y = tb.bottom() + gap;                  // 上面也塞不下就还是往下

        Rect cs = scene_.client_screen_rect();
        float s = scene_.dpi_scale();
        if (s <= 0.f) s = 1.f;
        float x = tb.x;
        if (x + w > cr.right()) x = std::max(cr.x, cr.right() - w);
        Rect screen_px(cs.x + x * s, cs.y + y * s, w * s, h * s);
        if (!show_popup(dp, screen_px, down ? PopupDir::Down : PopupDir::Up)) return nullptr;
        return dp;
    }

    // ---- 取色器：**唤起是程序的事** ----
    // 1) 回填到输入框：pick_color(box, kw::format(L"#{hex}"), ...)
    // 2) 只收回调（自己写变量）：pick_color(kw::format(L"rgb({r}, {g}, {b})"),
    //                                    kw::on_pick_color([&](int r, int g, int b){...}))
    // 面板复用下拉框那套弹层窗口与展开动画；确定/点色板才回填，取消不回填。
    template <class... Args>
    std::shared_ptr<ColorPicker> pick_color(const std::shared_ptr<Widget>& target, Args&&... args) {
        return make_color_picker(target, std::forward<Args>(args)...);
    }
    // 不指定回填位置：值只从 kw::on_pick_color 收（程序自己写变量）。
    // 这里排掉"第一个参数本身就是输入框"的情形，否则会和上面那个重载抢。
    template <class K, class... Args,
              typename = std::enable_if_t<!std::is_convertible<K, std::shared_ptr<Widget>>::value>>
    std::shared_ptr<ColorPicker> pick_color(K&& first, Args&&... args) {
        return make_color_picker(std::shared_ptr<Widget>(), std::forward<K>(first),
                                 std::forward<Args>(args)...);
    }

    template <class... Args>
    std::shared_ptr<ColorPicker> make_color_picker(const std::shared_ptr<Widget>& target,
                                                   Args&&... args) {
        if (!hwnd_) return nullptr;
        auto cp = std::make_shared<ColorPicker>(std::forward<Args>(args)...);
        auto box = std::dynamic_pointer_cast<TextBox>(target);

        std::wstring pat = cp->color_format.empty() ? std::wstring(L"#{hex}") : cp->color_format;
        if (!cp->color_given && box && !box->text.empty()) {
            int rr = 0, gg = 0, bb = 0;
            if (parse_color(box->text, rr, gg, bb)) cp->set_rgb(rr, gg, bb);   // 从框里反解初值
        }

        auto user_cb = cp->on_pick_color;
        cp->on_pick_color = [this, box, pat, user_cb](int r, int g, int b) {
            if (box) {
                box->set_text(format_color(r, g, b, pat));
                if (!box->change_name.empty()) scene_.fire(box->change_name, box.get(), true);
            }
            if (user_cb) user_cb(r, g, b);
            close_popup();
        };
        cp->on_close_request = [this]() { close_popup(); };

        Rect cr = scene_.client_rect();
        float w = cp->preferred_width();
        float h = cp->preferred_height();
        float gap = 6.f;
        Rect pos;
        if (target) {
            Rect tb = target->bounds;
            bool down = (tb.bottom() + gap + h <= cr.bottom() + 0.5f);
            float y = down ? (tb.bottom() + gap) : (tb.y - gap - h);
            if (y < cr.y) y = tb.bottom() + gap;
            float x = tb.x;
            if (x + w > cr.right()) x = std::max(cr.x, cr.right() - w);
            pos = Rect(x, y, w, h);
        } else {
            pos = Rect(cr.x + (cr.w - w) * 0.5f, cr.y + (cr.h - h) * 0.5f, w, h);
        }
        Rect cs = scene_.client_screen_rect();
        float s = scene_.dpi_scale();
        if (s <= 0.f) s = 1.f;
        Rect screen_px(cs.x + pos.x * s, cs.y + pos.y * s, pos.w * s, pos.h * s);
        PopupDir dir = (target && pos.y < target->bounds.y) ? PopupDir::Up : PopupDir::Down;
        if (!show_popup(cp, screen_px, dir)) return nullptr;
        return cp;
    }

    // 从第 level 层菜单的某一行旁边再拉出一层（右边放不下就翻到左边）
    bool show_submenu(std::shared_ptr<PopupPanel> panel, int level, int row) {
        if (!hwnd_ || !panel || level < 0 || level >= (int)popups_.size()) return false;
        PopupWin* parent = popups_[(size_t)level].get();
        if (!parent || !parent->visible()) return false;
        Rect row_screen = parent->row_screen_px(row);
        if (row_screen.w <= 1.f) return false;

        float s = scale_ > 0.f ? scale_ : 1.f;
        float w = parent->panel_px().w;                       // 和父菜单等宽
        float h = PopupPanel::content_height(panel->items, panel->row_h, panel->pad) * s;

        MONITORINFO mi{sizeof(mi)};
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        GetMonitorInfoW(mon, &mi);
        Rect work((float)mi.rcWork.left, (float)mi.rcWork.top,
                  (float)(mi.rcWork.right - mi.rcWork.left),
                  (float)(mi.rcWork.bottom - mi.rcWork.top));

        // 子菜单紧贴父面板的边（0 间距）：父子两块的 1px 深色描边叠在同一条缝上，
        // 才像 Edge 那样"看得出边界"。留 2px 空隙会变成一道背景色的缝，看着就是没对齐。
        Rect pn = parent->panel_px();
        bool right_ok = pn.right() + w <= work.right();
        PopupDir dir = right_ok ? PopupDir::Right : PopupDir::Left;
        float x = right_ok ? pn.right() : pn.x - w;
        x = std::max(work.x + 2.f, std::min(x, work.right() - w - 2.f));   // 左右都放不下时也别出界

        // 垂直方向：**整块**放进工作区，绝不因为底下压到任务栏就把面板剪短。
        // 1) 顶边对齐唤出它的那一行、往下贴 —— 放得下就用这个（和以前一样）；
        // 2) 放不下就把底边对齐这一行底边往上翻（Windows 的子菜单就是这么翻的）；
        // 3) 上下都塞不下（面板比整个工作区还高）才截到空间更大的一侧。
        float top_limit = work.y + 2.f;
        float bot_limit = work.bottom() - 2.f;
        float y = row_screen.y;
        float hh = h;
        if (y + hh > bot_limit) {
            float up_y = row_screen.bottom() - hh;
            if (up_y >= top_limit) {
                y = up_y;
            } else {
                float below = bot_limit - row_screen.y;
                float above = row_screen.bottom() - top_limit;
                if (below >= above) { y = row_screen.y; hh = below; }
                else { hh = above; y = row_screen.bottom() - hh; }
            }
        }
        if (hh < 1.f) return false;
        bool ok = spawn_popup(level + 1, std::move(panel), Rect(x, y, w, hh), dir);
        if (ok && parent->panel()) parent->panel()->open_child_row = row;   // 记下"这一行开着子菜单"
        return ok;
    }

    int popup_depth() const { return (int)popups_.size(); }
    // 诊断：这一层最近一帧的绘制耗时（含整块重建/局部合成两种路径）
    double popup_draw_ms(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return -1.0;
        return popups_[(size_t)level]->dbg_draw_ms();
    }
    // 诊断：这一层当前帧某块像素的指纹（自测用，验证"中间帧真的和终态不同"）
    uint64_t popup_area_hash(int level, int x, int y, int w, int h) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return 0;
        return popups_[(size_t)level]->area_hash(x, y, w, h);
    }
    bool popup_band_differs(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return false;
        return popups_[(size_t)level]->band_differs();
    }
    int popup_compose_count(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return -1;
        return popups_[(size_t)level]->compose_count();
    }
    void popup_band_shape(int level, int& w, int& h, int& old_sz, int& new_sz) const {
        w = h = old_sz = new_sz = 0;
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return;
        popups_[(size_t)level]->band_shape(w, h, old_sz, new_sz);
    }
    // 这一套弹出层（含每一层子菜单）默认的"悬浮展开"行为，由下拉框的参数写进来
    void set_popup_hover(bool on, double delay_ms) {
        popup_hover_open_ = on;
        popup_hover_delay_ = delay_ms;
    }
    // 第 level 层的弹层窗口句柄（自测里可以直接往里发鼠标消息）
    HWND popup_hwnd(int level) const {
        if (level < 0 || level >= (int)popups_.size()) return nullptr;
        return popups_[(size_t)level] ? popups_[(size_t)level]->window() : nullptr;
    }
    std::shared_ptr<PopupPanel> popup_panel(int level) const {
        if (level < 0 || level >= (int)popups_.size()) return nullptr;
        return popups_[(size_t)level] ? popups_[(size_t)level]->panel() : nullptr;
    }
    // 第 level 层面板在屏幕上的矩形（像素），自测打印几何用
    Rect popup_panel_screen_px(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return Rect();
        return popups_[(size_t)level]->panel_px();
    }
    // 该层弹窗每次出帧的时间戳（诊断帧间隔）
    std::vector<double> popup_frame_times(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return {};
        return popups_[(size_t)level]->frame_times();
    }
    // 曲线诊断：第 level 层当前的动画进度与可见高度（像素）
    float popup_reveal_e(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return 0.f;
        return popups_[(size_t)level]->reveal_e_now();
    }
    float popup_visible_h(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return 0.f;
        return popups_[(size_t)level]->visible_h_px_now();
    }
    // 上一帧耗时拆成两段：画（D2D 贴图+裁剪） / 上传（UpdateLayeredWindow）
    std::pair<double, double> popup_frame_cost(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return {0.0, 0.0};
        return {popups_[(size_t)level]->dbg_draw_ms(), popups_[(size_t)level]->dbg_ulw_ms()};
    }
    std::tuple<double, double, double> popup_draw_parts(int level) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level])
            return {0.0, 0.0, 0.0};
        return popups_[(size_t)level]->dbg_draw_parts();
    }
    // 直接连调 N 次 render()，返回每次的平均毫秒数（绕开 WM_TIMER 的 15.6ms 节拍）
    double popup_render_bench(int level, int n) {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level]) return -1.0;
        auto* w = popups_[(size_t)level].get();
        return w->bench_render(n);
    }
    // 强制该层下一帧重建预渲染位图（bench 用）
    void popup_invalidate_art(int level) {
        if (level >= 0 && level < (int)popups_.size() && popups_[(size_t)level])
            popups_[(size_t)level]->invalidate_art();
    }
    // 每一层面板在屏幕上的矩形（自测/截图核对用）
    std::vector<Rect> popup_rects() const {
        std::vector<Rect> v;
        for (auto& p : popups_) if (p) v.push_back(p->screen_panel_rect());
        return v;
    }
    void close_popup() {
        for (auto& p : popups_) if (p) p->request_close();
    }
    void close_popup_now() {
        for (auto& p : popups_) if (p) p->hide_now(true);
    }

private:
    // 这个窗口是不是本线程自己画的（主窗口或任何一层弹层）。
    // 用来区分"用户点到别处去了"和"用户正在点我们自己的弹层"。
    bool own_window(HWND h) const {
        if (!h) return false;
        if (h == hwnd_) return true;
        for (auto& p : popups_) {
            if (p && p->window() == h) return true;
        }
        return GetWindowThreadProcessId(h, nullptr) == GetCurrentThreadId();
    }

    int submenu_src_level_ = -1;   // 最近一次"哪一层的面板要点子菜单"

    // 点击点落在哪一层弹层的窗口范围内（-1 = 都不在）。screen 是物理像素。
    int popup_level_at_screen(float sx, float sy) const {
        for (int i = (int)popups_.size() - 1; i >= 0; --i) {
            HWND ph = popups_[(size_t)i] ? popups_[(size_t)i]->window() : nullptr;
            RECT wr{};
            if (!ph || !GetWindowRect(ph, &wr)) continue;
            if (sx >= (float)wr.left && sx <= (float)wr.right &&
                sy >= (float)wr.top && sy <= (float)wr.bottom) return i;
        }
        return -1;
    }

    // 屏幕物理像素 -> 某一层弹层的客户区逻辑坐标（层已收掉时返回 (-1,-1)）
    Point popup_client_pt(int level, float sx, float sy) const {
        if (level < 0 || level >= (int)popups_.size() || !popups_[(size_t)level])
            return Point(-1.f, -1.f);
        HWND ph = popups_[(size_t)level]->window();
        RECT wr{};
        if (!ph || !GetWindowRect(ph, &wr)) return Point(-1.f, -1.f);
        float s = scale_ > 0.f ? scale_ : 1.f;
        return Point((sx - (float)wr.left) / s, (sy - (float)wr.top) / s);
    }

    // 在指定层级创建一层弹窗：screen_px 是屏幕物理像素，内部换算成客户区逻辑坐标
    bool spawn_popup(int level, std::shared_ptr<PopupPanel> panel, const Rect& screen_px, PopupDir dir) {
        if (!hwnd_ || !panel) return false;
        POINT tl{0, 0};
        ClientToScreen(hwnd_, &tl);
        float s = scale_ > 0.f ? scale_ : 1.f;
        Rect logical((screen_px.x - (float)tl.x) / s, (screen_px.y - (float)tl.y) / s,
                     screen_px.w / s, screen_px.h / s);

        // 每一层都继承"悬浮展开"的默认；面板自己设过（hover_open_mode>=0）就不动
        if (panel->hover_open_mode < 0) {
            panel->hover_open = popup_hover_open_;
            panel->hover_delay_ms = popup_hover_delay_;
        }
        panel->popup_level = level;                 // 子菜单靠它知道该开在哪一层右边
        panel->open_child_row = -1;
        // 离开带 ▸ 的行：立刻把更深的菜单收掉（参考实现没有延迟）。
        // 唯一保留的 60ms 只是"看一眼指针是不是已经跨进子菜单"——跨进去就不收，
        // 否则从父菜单划进子菜单的瞬间会被自己关掉。
        const int my_level = level;
        panel->on_submenu_leave = [this, my_level]() { this->schedule_close_above(my_level); };
        panel->child_open = [this, my_level]() { return (int)popups_.size() > my_level + 1; };

        if (level > 0 && level < (int)popups_.size()) {
            // 从这一层往上重建（重复展开同一层时不留旧窗口）
            if (popups_[(size_t)level - 1]) popups_[(size_t)level - 1]->panel()->open_child_row = -1;
            while ((int)popups_.size() > level) {
                popups_.back()->destroy();
                popups_.pop_back();
            }
        }
        auto pw = std::make_unique<PopupWin>();
        bool ok = pw->show(hwnd_, std::move(panel), screen_px, logical, dir, scene_.palette, s,
                           !headless_);
        if (!ok) return false;
        popups_.push_back(std::move(pw));
        return true;
    }

    // 收掉第 level 层以下的菜单（level 这一层保留）
    void close_above(int level) {
        if (level >= 0 && level < (int)popups_.size() && popups_[(size_t)level] &&
            popups_[(size_t)level]->panel())
            popups_[(size_t)level]->panel()->open_child_row = -1;   // 这一层的子菜单收掉了
        while ((int)popups_.size() > level + 1) {
            popups_.back()->hide_now(true);
            popups_.back()->destroy();
            popups_.pop_back();
        }
    }

    // 离开带 ▸ 的行：立刻收掉更深的菜单。60ms 只是"看一眼指针有没有已经跨进子菜单"，
    // 不是延迟关闭——指针在子菜单里就不收（不然从父菜单划进子菜单会断）。
    void schedule_close_above(int level) {
        if ((int)popups_.size() <= level + 1) return;
        if (headless_) { close_above(level); return; }   // 自测里没有真实指针，直接收
        post(60, [this, level]() {
            if ((int)popups_.size() <= level + 1) return;
            POINT cur{0, 0};
            GetCursorPos(&cur);
            for (int i = level + 1; i < (int)popups_.size(); ++i) {
                if (!popups_[(size_t)i]) continue;
                Rect r = popups_[(size_t)i]->screen_panel_rect();
                if (r.w <= 0.f) continue;
                if ((float)cur.x >= r.x - 6.f && (float)cur.x <= r.right() + 6.f &&
                    (float)cur.y >= r.y - 6.f && (float)cur.y <= r.bottom() + 6.f) return;   // 指针在里面
            }
            close_above(level);
        });
    }

public:
    bool popup_visible() const {
        for (auto& p : popups_) if (p && p->visible()) return true;
        return false;
    }
    // "真的开着"：有弹层，而且不是在播收起动画。收起中的层不算数——
    // 用户点了别的地方、弹层正在收，这时再聚焦另一个框要能立刻弹新的出来。
    bool popup_open() const {
        for (auto& p : popups_)
            if (p && p->visible() && !p->is_closing()) return true;
        return false;
    }
    HWND popup_hwnd() const {
        for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
            if (*it && (*it)->visible()) return (*it)->window();
        return nullptr;
    }
    std::shared_ptr<PopupPanel> popup_panel() const {
        for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
            if (*it && (*it)->visible()) return (*it)->panel();
        return nullptr;
    }

    // 延时一次回调（毫秒）。可以排多个，互不覆盖
    void post(int delay_ms, std::function<void()> fn) {
        if (!hwnd_) return;
        UINT id = kPostTimerBase + next_timer_++;
        delayed_[id] = std::move(fn);
        SetTimer(hwnd_, id, (UINT)delay_ms, nullptr);
    }

    // 离屏连拍用：把各层弹窗的展开进度钉死成一个值再出图（-1 = 恢复按真实时间走）
    void set_popup_e(float e) {
        for (auto& p : popups_) if (p) p->set_e_override(e);
    }

    // 把弹出层自己的分层像素导出成 PNG（带 alpha），视觉回归/排查阴影用
    bool save_popup_png(const std::wstring& path) {
        int w = 0, h = 0;
        const void* px = popups_.empty() ? nullptr : popups_.front()->pixels(w, h);
        if (!px || w <= 0 || h <= 0) return false;
        return save_bgra_png(px, w, h, path);
    }

    // 指定层：0 = 最外层菜单，1 = 它的子菜单……
    bool save_popup_png(int level, const std::wstring& path) {
        int w = 0, h = 0;
        const void* px = (level >= 0 && level < (int)popups_.size() && popups_[(size_t)level])
                             ? popups_[(size_t)level]->pixels(w, h)
                             : nullptr;
        if (!px || w <= 0 || h <= 0) return false;
        return save_bgra_png(px, w, h, path);
    }


    // 把当前界面（含已展开的弹出层）存成 PNG，倍率 1.5（1120x720 -> 1680x1080）
    bool save_png(const std::wstring& path) {
        if (!hwnd_ || !scene_.root) return false;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        float lw = (float)(rc.right - rc.left) / scale_;
        float lh = (float)(rc.bottom - rc.top) / scale_;
        std::shared_ptr<PopupPanel> p;
        Rect pr;
        if (!popups_.empty()) popups_.front()->preview_target(p, pr);
        float th = (chrome_mode_ == Chrome::Custom) ? title_h_ : 0.f;
        return render_to_png(scene_.root, scene_.palette, lw, lh, scale_ * 1.5f, path, p, pr, th,
                             [this](Painter& pp, float w) { paint_chrome_into(pp, w); },
                             surface_acrylic_);
    }

    // 离屏渲染前把控件树准备好（snapshot 用）
    void prepare_offscreen(Painter& p, Size logical) {
        scene_.root->attach(&scene_);
        scene_.layout(p, logical);
    }

private:
    // -- 主题 --
    void rebuild_palette() {
        bool dark = theme_ == Theme::Dark || (theme_ == Theme::Auto && !system_is_light());
        scene_.dark = dark;
        scene_.palette = make_palette(dark, accent_);
        if (painter_.valid()) {
            painter_.set_palette(scene_.palette);
        }
    }

    // 系统 build 号（Win11 是 22000+，只有它才认 caption/text/border 三色）
    static int os_build() {
        static int cached = -1;
        if (cached < 0) {
            cached = 0;
            HMODULE nt = GetModuleHandleW(L"ntdll.dll");
            if (nt) {
                typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
                auto fn = (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion");
                OSVERSIONINFOW vi{};
                vi.dwOSVersionInfoSize = sizeof(vi);
                if (fn && fn(&vi) == 0) cached = (int)vi.dwBuildNumber;
            }
        }
        return cached;
    }

    static COLORREF to_colorref(const Color& c) {
        return RGB((int)std::lround(c.r * 255.f), (int)std::lround(c.g * 255.f),
                   (int)std::lround(c.b * 255.f));
    }

    // 标题栏（"操作栏"）跟着主题走：沉浸式深色 + 显式 caption/text/border 三色
    void apply_dwm() {
        if (!hwnd_) return;
        BOOL dark = scene_.dark ? TRUE : FALSE;
        // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE（Win10 1809 起；更早版本是 19）
        if (FAILED(DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd_, 19, &dark, sizeof(dark));

        // Win11 22000+：直接指定标题栏 / 标题文字 / 外框颜色，切换是"立刻换色"，
        // 不用等 DWM 自己推算，也就不会出现"深色界面配浅色标题栏"。
        // Win10 不认这三个属性（返回 E_INVALIDARG），只能靠上面的沉浸式深色。
        if (os_build() >= 22000) {
            COLORREF cap = to_colorref(scene_.palette.window_bg);
            COLORREF txt = to_colorref(scene_.palette.text);
            COLORREF brd = to_colorref(scene_.dark ? Color::hex(0x2E2E2E) : Color::hex(0xDFDFDF));
            const DWORD kCaptionColor = 35, kTextColor = 36, kBorderColor = 34;
            DwmSetWindowAttribute(hwnd_, kCaptionColor, &cap, sizeof(cap));
            DwmSetWindowAttribute(hwnd_, kTextColor, &txt, sizeof(txt));
            DwmSetWindowAttribute(hwnd_, kBorderColor, &brd, sizeof(brd));
            // 33 = DWMWA_WINDOW_CORNER_PREFERENCE, 2 = DWMWCP_ROUND（Win11）
            int corner = 2;
            DwmSetWindowAttribute(hwnd_, 33, &corner, sizeof(corner));
        }

        // 关键：DWM 缓存帧，属性改完不刷非客户区就要等下次激活才看见（"切焦点才变色"）。
        // SWP_FRAMECHANGED + RedrawWindow(RDW_FRAME) 强制立刻重画。
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        // Win10 的沉浸式深色只在"激活态"重算：非激活窗口里改完经常不生效（要切焦点才变）。
        // 补一条 WM_NCACTIVATE(TRUE) 只是让非客户区按激活样式重画一遍，不会真的抢焦点。
        SendMessageW(hwnd_, WM_NCACTIVATE, TRUE, 0);
    }

    // ---- 窗口样式：系统栏 / 自绘栏 ----
    void apply_style() {
        if (!hwnd_) return;
        LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        if (chrome_mode_ == Chrome::Custom) {
            // 去掉 WS_CAPTION（那一圈就是系统标题栏），保留粗边框 + 最小/最大化位：
            // 留着这些位，贴边吸附 / 任务栏右键菜单 / Win+方向键 这类系统行为照旧。
            style &= ~(LONG_PTR)WS_CAPTION;
            style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        } else {
            style |= WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        }
        SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
        // 只让非客户区重算，窗口矩形保持不动（切回系统栏时客户区变窄，窗口大小不变）
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    }

    // ---- 背景材质 ----
    // Win11 22621+ 认 DWMWA_SYSTEMBACKDROP_TYPE（2 = Mica，3 = Acrylic）；Win10 只有
    // SetWindowCompositionAttribute 的 ACCENT_ENABLE_ACRYLICBLURBEHIND。
    // 两点现实约束：
    //   ① 材质是系统画在窗口"背后"的东西，客户区被 D2D 逐个像素画成不透明，
    //      所以真正能透出来的只有系统栏/边框那一圈；
    //   ② 要让客户区透出桌面，得换 DXGI swap chain + WS_EX_NOREDIRECTIONBITMAP，
    //      那是另一套渲染后端，不在这个库的 HDC/D2D 路径里。
    // 返回值 = 系统有没有接受这个属性，程序可以拿它决定要不要自己画磨砂底。
    bool apply_backdrop() {
        if (!hwnd_) return false;
        if (backdrop_ == Backdrop::None) {
            if (os_build() >= 22621) {
                int t = 1;   // DWMSBT_NONE
                DwmSetWindowAttribute(hwnd_, 38, &t, sizeof(t));
            }
            set_accent_policy(0);
            return false;
        }
        bool dark = backdrop_dark_tint_ || scene_.dark;
        if (os_build() >= 22621) {
            int t = (backdrop_ == Backdrop::Mica) ? 2 : 3;
            int back = -1;
            HRESULT s = DwmSetWindowAttribute(hwnd_, 38, &t, sizeof(t));
            HRESULT g = DwmGetWindowAttribute(hwnd_, 38, &back, sizeof(back));
            BOOL d = dark ? TRUE : FALSE;
            DwmSetWindowAttribute(hwnd_, 20, &d, sizeof(d));
            // Win10 也对 38 号返回 S_OK 却什么也不做，所以必须读回来核对
            return SUCCEEDED(s) && SUCCEEDED(g) && back == t;
        }
        // Win10：没有 Mica，只有亚克力；而且客户区是不透明像素，材质只在系统栏那一圈可见
        if (backdrop_ == Backdrop::Mica) return false;
        return set_accent_policy(4, dark ? 0xCC1A1A1A : 0xCCF2F2F2);
    }

    // 未文档化的 user32!SetWindowCompositionAttribute（Win10 1803+ 都还在）
    bool set_accent_policy(int state, DWORD gradient = 0) {
        struct ACCENT_POLICY { int state; int flags; DWORD gradient; int anim; };
        struct WC_DATA { int attrib; void* data; DWORD size; };
        typedef BOOL(WINAPI* FnSetWCA)(HWND, WC_DATA*);
        static FnSetWCA fn = nullptr;
        static bool looked = false;
        if (!looked) {
            looked = true;
            HMODULE u = GetModuleHandleW(L"user32.dll");
            if (u) fn = (FnSetWCA)GetProcAddress(u, "SetWindowCompositionAttribute");
        }
        if (!fn) return false;
        ACCENT_POLICY ap{};
        ap.state = state;          // 0 关 / 3 模糊 / 4 亚克力
        ap.flags = 2;
        ap.gradient = gradient;
        WC_DATA d{19 /*WCA_ACCENT_POLICY*/, &ap, sizeof(ap)};
        return fn(hwnd_, &d) != FALSE;
    }

    // 标题栏按钮矩形（逻辑坐标）：从右往左依次 关闭 → 最大化 → 最小化
    Rect chrome_btn_rect(int i, float w) const {
        float bw = 46.f, h = title_h_;
        float x = w;
        if (show_close_) { x -= bw; if (i == 2) return Rect(x, 0.f, bw, h); }
        if (show_max_)   { x -= bw; if (i == 1) return Rect(x, 0.f, bw, h); }
        if (show_min_)   { x -= bw; if (i == 0) return Rect(x, 0.f, bw, h); }
        return Rect(0.f, 0.f, 0.f, 0.f);
    }

    int chrome_button_at(const Point& p, float w) const {
        if (p.y < 0.f || p.y >= title_h_) return -1;
        for (int i = 0; i < 3; ++i) {
            Rect r = chrome_btn_rect(i, w);
            if (r.w > 0.f && p.x >= r.x && p.x < r.right()) return i;
        }
        return -1;
    }

    // 画自绘标题栏（在内容画完之后画，盖住顶部 title_h 那一条）
    void paint_chrome(float w) { paint_chrome_into(painter_, w); }

    void paint_chrome_into(Painter& p, float w) {
        const Palette& pal = scene_.palette;
        if (surface_acrylic_) {
            p.fill_surface_glass(Rect(0.f, 0.f, w, title_h_), scene_.dark);   // 标题栏同一层玻璃
        } else {
            p.fill_rect(Rect(0.f, 0.f, w, title_h_), pal.window_bg);
        }

        float btn_left = w;
        for (int i = 0; i < 3; ++i) {
            Rect r = chrome_btn_rect(i, w);
            if (r.w > 0.f && r.x < btn_left) btn_left = r.x;
        }
        const std::wstring& tt = title_text();
        if (!tt.empty() && btn_left > 30.f) {
            p.text(tt, Rect(14.f, 0.f, btn_left - 22.f, title_h_), TextScale::Body, pal.text,
                   HAlign::Start, VAlign::Middle, false, true);
        }

        Color hot      = scene_.dark ? Color::hex(0x2E2E2E) : Color::hex(0xE9E9E9);
        Color hot_down = scene_.dark ? Color::hex(0x3A3A3A) : Color::hex(0xDCDCDC);
        Color close_hot  = Color::hex(0xC42B1C);   // Win11 关闭按钮的红，深浅主题同色
        Color close_down = Color::hex(0xB0261A);
        for (int i = 0; i < 3; ++i) {
            Rect r = chrome_btn_rect(i, w);
            if (r.w <= 0.f) continue;
            bool hov = (h_chrome_btn_ == i), dn = (down_chrome_btn_ == i);
            if (i == 2) {
                if (dn) p.fill_rect(r, close_down);
                else if (hov) p.fill_rect(r, close_hot);
            } else {
                if (dn) p.fill_rect(r, hot_down);
                else if (hov) p.fill_rect(r, hot);
            }
            Color fg = (i == 2 && (hov || dn)) ? Color(1.f, 1.f, 1.f, 1.f) : pal.text;
            std::wstring g = L"\uE921";                                  // 最小化
            if (i == 1) g = IsZoomed(hwnd_) ? L"\uE923" : L"\uE922";      // 还原 / 最大化
            if (i == 2) g = L"\uE8BB";                                   // 关闭
            p.glyph(g, r, 10.f, fg);
        }
    }

    // -- 窗口 --
    bool create() {
        ensure_com();

        static const wchar_t* kClass = L"w11ui_WindowClass";
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = &Window::WndProcStatic;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = cursor_arrow();
            wc.hbrBackground = nullptr;          // 自己画，避免闪白
            wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));   // exe 自带图标（没有就是 null，无害）
            wc.hIconSm = wc.hIcon;
            wc.lpszClassName = kClass;
            if (!RegisterClassExW(&wc)) return false;
            registered = true;
        }

        int w = (int)width_, h = (int)height_;
        DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        if (chrome_mode_ == Chrome::Custom) {
            style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU |
                    WS_CLIPCHILDREN;
        }
        // 自绘栏没有系统标题栏，AdjustWindowRectEx 算出的外框就少了那一条；
        // 把 title_h 自己补进去，客户区才是程序要求的 h。
        // Custom 时客户区盖满整个窗口（WM_NCCALCSIZE 返回 0），所以窗口尺寸就是
        // 内容尺寸 + title_h，不能再让 AdjustWindowRectEx 往里加边框。
        int ww, wh;
        if (chrome_mode_ == Chrome::Custom) {
            ww = w;
            wh = h + (int)title_h_;
        } else {
            RECT rc{0, 0, w, h};
            AdjustWindowRectEx(&rc, style, FALSE, 0);
            ww = rc.right - rc.left;
            wh = rc.bottom - rc.top;
        }
        int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
        int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;

        hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kClass, title_.c_str(), style, sx, sy, ww, wh,
                                nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd_) return false;

        scale_ = (float)GetDpiForWindow(hwnd_) / 96.f;
        if (scale_ <= 0.f) scale_ = 1.f;
        scene_.scale = scale_;
        apply_dwm();
        apply_backdrop();
        ensure_target();
        SetTimer(hwnd_, 1, 15, nullptr);   // 补帧时钟（约 66Hz 上限）
        // 光标闪烁时钟：250ms 采样一次（光标 0.5s 亮 / 0.5s 灭，四次采样必然踩到切换点）。
        // 之前只在"快到切换点"时补帧，但那一刻本身要靠重绘才会被判断到 —— 没有第一帧
        // 就没有后续帧，光标就定在亮着不动了。
        SetTimer(hwnd_, 3, 250, nullptr);
        return true;
    }

    void destroy_target() {
        painter_ = Painter{};
        rt_.reset();
    }

    bool ensure_target() {
        if (!hwnd_) return false;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        UINT pw = (UINT)std::max(1L, (long)(rc.right - rc.left));
        UINT ph = (UINT)std::max(1L, (long)(rc.bottom - rc.top));

        if (rt_) {
            rt_->Resize(D2D1_SIZE_U{pw, ph});
        } else {
            D2D1_RENDER_TARGET_PROPERTIES props{};
            props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
            props.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM,
                                                  D2D1_ALPHA_MODE_IGNORE};
            props.dpiX = props.dpiY = 96.f * scale_;
            props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
            props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
            D2D1_HWND_RENDER_TARGET_PROPERTIES hp{};
            hp.hwnd = hwnd_;
            hp.pixelSize = D2D1_SIZE_U{pw, ph};
            hp.presentOptions = D2D1_PRESENT_OPTIONS_NONE;
            if (FAILED(d2d_factory()->CreateHwndRenderTarget(props, hp, rt_.put()))) return false;
        }
        if (!painter_.valid()) {
            painter_ = Painter{};
            painter_.init(rt_.get(), scene_.palette, scale_, false);
        }
        return true;
    }

    void paint_now() {
        if (!hwnd_) return;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        (void)hdc;

        if (!ensure_target()) {
            EndPaint(hwnd_, &ps);
            return;
        }

        RECT rc;
        GetClientRect(hwnd_, &rc);
        Size logical((float)(rc.right - rc.left) / scale_, (float)(rc.bottom - rc.top) / scale_);

        painter_.set_palette(scene_.palette);
        painter_.set_scale(scale_);
        painter_.begin(scene_.palette.window_bg);
        if (surface_acrylic_) {
            // 客户区玻璃底：很轻的一层，浅色只提亮、深色只亮上部（不压暗、不发灰）
            painter_.fill_surface_glass(Rect(0.f, 0.f, logical.w, logical.h), scene_.dark);
        }
        // 自绘标题栏：内容整体下移 title_h，画面坐标从 (0, title_h) 开始
        float th = (chrome_mode_ == Chrome::Custom) ? title_h_ : 0.f;
        float content_h = logical.h - th;
        if (content_h < 1.f) content_h = 1.f;
        painter_.push_translate(0.f, th);
        scene_.layout(painter_, Size(logical.w, content_h));
        scene_.paint(painter_);
        painter_.pop_translate();
        if (chrome_mode_ == Chrome::Custom) paint_chrome(logical.w);
        if (painter_.end() == D2DERR_RECREATE_TARGET) {
            // 设备丢失（切换显示器 / 驱动重置）：丢掉目标，下一帧自动重建
            destroy_target();
        }

        EndPaint(hwnd_, &ps);

        if (!ready_fired_) {
            ready_fired_ = true;
            PostMessageW(hwnd_, WM_APP + 1, 0, 0);   // 别在 WM_PAINT 里直接回调，避免重入
        }
    }

    void refresh_cursor() {
        if (!hwnd_) return;
        Widget* h = scene_.hot;
        if (h && h->text_cursor()) {
            SetCursor(cursor_ibeam());
            cursor_hand_ = false;
        } else if (h && h->hand_cursor()) {
            SetCursor(cursor_hand());
            cursor_hand_ = true;
        } else {
            SetCursor(cursor_arrow());
            cursor_hand_ = false;
        }
    }

    Point to_logical(LPARAM lp) const {
        float y = (float)GET_Y_LPARAM(lp) / scale_;
        if (chrome_mode_ == Chrome::Custom) y -= title_h_;   // 内容从标题栏下面开始
        return Point((float)GET_X_LPARAM(lp) / scale_, y);
    }

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        {
            static int traced = 0;
            if (traced < 40 && msg != WM_MOUSEMOVE && msg != WM_PAINT && msg != 0x0118 /*WM_SYSTIMER*/) {
                ++traced;
            }
        }
        switch (msg) {
            case WM_NCCALCSIZE:
                if (chrome_mode_ == Chrome::Custom && wp) {
                    // 客户区 = 整个窗口：系统那条标题栏不再占位。
                    // 最大化时把顶边补回来一帧（否则内容被屏幕边缘吃掉一条）。
                    if (IsZoomed(hwnd_) && lp) {
                        NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lp;
                        p->rgrc[0].top += GetSystemMetrics(SM_CYFRAME) +
                                          GetSystemMetrics(SM_CXPADDEDBORDER);
                    }
                    return 0;
                }
                break;

            case WM_NCHITTEST: {
                LRESULT r = DefWindowProcW(hwnd_, msg, wp, lp);
                if (chrome_mode_ != Chrome::Custom) return r;
                if (r != HTCLIENT) return r;        // 系统已经判成边框了，照用
                POINT sp{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                // 客户区已经盖满整个窗口，系统算不出"边框带"了，这里自己判：
                // 离窗口边 8 逻辑像素内就是缩放边框（最大化时没有边框）。
                if (!IsZoomed(hwnd_)) {
                    RECT wr;
                    GetWindowRect(hwnd_, &wr);
                    LONG fx = (LONG)(8.f * scale_);
                    bool L = sp.x < wr.left + fx, R = sp.x >= wr.right - fx;
                    bool T = sp.y < wr.top + fx, B = sp.y >= wr.bottom - fx;
                    if (T && L) return HTTOPLEFT;
                    if (T && R) return HTTOPRIGHT;
                    if (B && L) return HTBOTTOMLEFT;
                    if (B && R) return HTBOTTOMRIGHT;
                    if (L) return HTLEFT;
                    if (R) return HTRIGHT;
                    if (T) return HTTOP;
                    if (B) return HTBOTTOM;
                }
                POINT c = sp;
                ScreenToClient(hwnd_, &c);
                RECT rc;
                GetClientRect(hwnd_, &rc);
                float w = (float)(rc.right - rc.left) / scale_;
                Point pt((float)c.x / scale_, (float)c.y / scale_);
                if (pt.y >= 0.f && pt.y < title_h_) {
                    if (chrome_button_at(pt, w) >= 0) return HTCLIENT;   // 按钮自己处理点击
                    return HTCAPTION;                                    // 拖窗口 / 双击最大化交给系统
                }
                return HTCLIENT;
            }

            case WM_ERASEBKGND:
                return 1;   // 由 Direct2D 全量绘制

            case WM_PAINT:
                if (IsIconic(hwnd_)) {   // 最小化时客户区是 0×0，别白画一遍
                    ValidateRect(hwnd_, nullptr);
                    return 0;
                }
                paint_now();
                return 0;

            case WM_SIZE:
                close_popup_now();   // 窗口尺寸变了，弹出层会错位
                // 最小化 / Win+D：停掉补帧时钟。否则窗口看不见了还在 66Hz 空转，
                // 而且这一刻前台窗口常是空的，很容易被 WM_ACTIVATE 当成"点了弹层"去抢前台。
                if (IsIconic(hwnd_)) KillTimer(hwnd_, 1);
                else SetTimer(hwnd_, 1, 15, nullptr);   // 和 create() 里同一个补帧时钟
                if (rt_) {
                    RECT rc;
                    GetClientRect(hwnd_, &rc);
                    rt_->Resize(D2D1_SIZE_U{(UINT)std::max(1L, (long)(rc.right - rc.left)),
                                            (UINT)std::max(1L, (long)(rc.bottom - rc.top))});
                }
                invalidate();
                return 0;

            case WM_MOVE:
                close_popup_now();
                return 0;

            case WM_ACTIVATE:
                if (LOWORD(wp) == WA_INACTIVE) {
                    // 最小化 / 按 Win+D 显示桌面时也会走到这里，而那一刻前台窗口经常是空的。
                    // 老代码"前台为空就把前台抢回来"会在这个时机反复 SetForegroundWindow，
                    // 系统判定这个窗口在最小化状态下硬要前台，于是任务栏按钮一直闪"有新东西"。
                    // 所以窗口一最小化就什么都不抢，只把弹层收掉。
                    if (IsIconic(hwnd_)) {
                        close_popup_now();
                        return 0;
                    }
                    // 点自己这一套弹层（分层 + NOACTIVATE 窗口）时，主窗口也会先收到
                    // WA_INACTIVE —— 系统会给"被点的 NOACTIVATE 窗口"发一次激活流程。
                    // 这时如果照旧收栈，正被点的那层在下落（DOWN）阶段就没了，松手（UP）
                    // 无处可去：表现就是"点了列表项，值没填上，窗口还变成了未激活的灰样子"。
                    // 只有前台真的换到别的线程/进程的窗口，才收栈。
                    HWND fg = GetForegroundWindow();
                    if (fg == hwnd_) return 0;                 // 前台还是自己，什么都不做
                    // 只有"弹层此刻还开着"才可能是点弹层造成的假失活；没有弹层就不要抢前台，
                    // 抢了会让别的程序/桌面以为我们还要前台，任务栏就开始闪。
                    if (popup_visible() && (!fg || own_window(fg))) {
                        SetForegroundWindow(hwnd_);
                        return 0;
                    }
                    close_popup_now();
                }
                return 0;

            case WM_KEYDOWN:
                // 模态层 / 输入框先吃键；吃不到才走弹层的 ESC 关
                if (scene_.modal_open() || scene_.focus) {
                    if (scene_.key_down((int)wp)) return 0;
                }
                if (wp == VK_ESCAPE && popup_visible()) {
                    close_popup();
                    return 0;
                }
                break;

            case WM_CHAR:
                // 输入框要字符；控制字符（回车/退格/ESC 的 WM_CHAR）不进输入框
                if (wp >= 0x20 && wp != 0x7F && scene_.char_input((wchar_t)wp)) return 0;
                break;

            case WM_APP + 1:
                if (on_ready_) {
                    auto fn = on_ready_;
                    on_ready_ = nullptr;
                    fn();
                }
                return 0;

            case WM_TIMER:
                if (wp >= kPostTimerBase) {           // Window::post 的一次性回调                    KillTimer(hwnd_, wp);
                    auto it = delayed_.find(wp);
                    if (it != delayed_.end()) {
                        auto fn = std::move(it->second);
                        delayed_.erase(it);
                        if (fn) fn();
                    }
                    return 0;
                }
                if (wp == 3) {
                    // 光标闪烁：只有"当前焦点是打字类控件"时才重绘，别的控件不白画
                    if (scene_.focus && scene_.focus->text_cursor()) invalidate();
                    return 0;
                }
                if (scene_.animating()) invalidate();
                return 0;

            case WM_GETMINMAXINFO: {
                MINMAXINFO* mi = (MINMAXINFO*)lp;
                mi->ptMinTrackSize.x = 420;
                mi->ptMinTrackSize.y = 280;
                return 0;
            }

            case WM_RBUTTONDOWN: {
                // 控件级右键菜单：命中哪个控件就弹它自己的 context_items
                if (popup_visible()) { close_popup(); invalidate(); return 0; }
                Point rpt = to_logical(lp);
                Widget* rw = scene_.target(rpt);
                if (rw && !rw->context_items.empty()) rw->open_context_menu(rpt);
                return 0;
            }

            case WM_MOUSEMOVE: {
                // 自绘栏：先更新按钮 hover（用客户像素坐标，不能走 to_logical —— 那里已经把 y 减了 title_h）
                if (chrome_mode_ == Chrome::Custom) {
                    RECT rc;
                    GetClientRect(hwnd_, &rc);
                    float w = (float)(rc.right - rc.left) / scale_;
                    Point raw((float)GET_X_LPARAM(lp) / scale_, (float)GET_Y_LPARAM(lp) / scale_);
                    int b = chrome_button_at(raw, w);
                    if (b != h_chrome_btn_) {
                        h_chrome_btn_ = b;
                        invalidate();
                    }
                }
                Point pt = to_logical(lp);
                scene_.mouse_move(pt);
                refresh_cursor();
                // 追踪鼠标离开，否则 hover 状态会卡住
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                return 0;
            }

            case WM_MOUSELEAVE:
                if (h_chrome_btn_ != -1) {
                    h_chrome_btn_ = -1;
                    invalidate();
                }
                scene_.mouse_leave();
                refresh_cursor();
                return 0;

            case WM_LBUTTONDOWN: {
                // 自绘栏上的按钮：按下就记账，松手还在同一个按钮上才算点中
                if (chrome_mode_ == Chrome::Custom && down_chrome_btn_ < 0) {
                    RECT rc;
                    GetClientRect(hwnd_, &rc);
                    float w = (float)(rc.right - rc.left) / scale_;
                    Point raw((float)GET_X_LPARAM(lp) / scale_, (float)GET_Y_LPARAM(lp) / scale_);
                    int b = chrome_button_at(raw, w);
                    if (b >= 0) {
                        down_chrome_btn_ = b;
                        h_chrome_btn_ = b;
                        SetCapture(hwnd_);
                        invalidate();
                        return 0;
                    }
                }
                if (popup_visible()) {
                    // 弹层本该独占鼠标。但分层窗口在个别情况下会把点击交给下层主窗口
                    // （首帧还没 UpdateLayeredWindow、或者点在窗口矩形里的全透明像素上）。
                    // 这里兜一层：落在某层弹层窗口里就记账，等 UP 再落值；真在外面的才收栈。
                    Point pt = to_logical(lp);
                    POINT tl{0, 0};
                    ClientToScreen(hwnd_, &tl);
                    float sc = scale_ > 0.f ? scale_ : 1.f;
                    // pt 是内容坐标（自绘栏时已经减掉 title_h），换回屏幕要把这一段加回来
                    float top = (chrome_mode_ == Chrome::Custom) ? title_h_ * sc : 0.f;
                    int lvl = popup_level_at_screen((float)tl.x + pt.x * sc,
                                                    (float)tl.y + pt.y * sc + top);
                    if (lvl >= 0) {
                        popup_click_level_ = lvl;
                        popup_click_pt_ = pt;
                        return 0;
                    }
                    // 点在弹层外面：收栈，并且把这一击照常交给场景。
                    // 交还焦点：不交还的话焦点还挂在输入框上，再点同一个框不会产生 focus 变化，
                    // "聚焦就弹键盘"的钩子再也不会触发（表现就是点了没反应）。
                    // 派发这一击：点另一个输入框时能立刻聚焦（否则得点两下）。
                    scene_.mouse_down(pt);
                    close_popup();
                    return 0;
                }
                SetCapture(hwnd_);
                scene_.mouse_down(to_logical(lp));
                invalidate();
                return 0;
            }

            case WM_LBUTTONUP:
                if (down_chrome_btn_ >= 0) {
                    int b = down_chrome_btn_;
                    down_chrome_btn_ = -1;
                    ReleaseCapture();
                    // 松手还在同一个按钮上才算点中（拖出去松手 = 取消，和系统按钮一样）
                    RECT rc;
                    GetClientRect(hwnd_, &rc);
                    float w = (float)(rc.right - rc.left) / scale_;
                    Point raw((float)GET_X_LPARAM(lp) / scale_, (float)GET_Y_LPARAM(lp) / scale_);
                    if (chrome_button_at(raw, w) == b) {
                        UINT cmd = SC_MINIMIZE;
                        if (b == 1) cmd = IsZoomed(hwnd_) ? SC_RESTORE : SC_MAXIMIZE;
                        if (b == 2) cmd = SC_CLOSE;
                        SendMessageW(hwnd_, WM_SYSCOMMAND, cmd, 0);
                    }
                    invalidate();
                    return 0;
                }
                if (popup_click_level_ >= 0) {
                    int lvl = popup_click_level_;
                    popup_click_level_ = -1;
                    Point pt = to_logical(lp);
                    POINT tl{0, 0};
                    ClientToScreen(hwnd_, &tl);
                    float sc = scale_ > 0.f ? scale_ : 1.f;
                    float top = (chrome_mode_ == Chrome::Custom) ? title_h_ * sc : 0.f;
                    Point c = popup_client_pt(lvl, (float)tl.x + pt.x * sc,
                                              (float)tl.y + pt.y * sc + top);
                    auto pnl = popup_panel(lvl);
                    if (pnl && c.x >= 0.f) {
                        if (pnl->bounds.contains(c)) {
                            pnl->hover = -1;
                            pnl->click(c);
                        } else {
                            close_popup();   // 点在窗口矩形里但落在面板外（阴影留白）：当点外面
                        }
                    }
                    invalidate();
                    return 0;
                }
                scene_.mouse_up(to_logical(lp));
                ReleaseCapture();
                invalidate();
                return 0;

            case WM_CAPTURECHANGED:
                down_chrome_btn_ = -1;
                if (scene_.press) {
                    scene_.press->on_press(false);
                    scene_.press = nullptr;
                }
                return 0;

            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    if (cursor_hand_) {
                        SetCursor(cursor_hand());
                        return TRUE;
                    }
                }
                break;

            case WM_DPICHANGED: {
                close_popup_now();
                float dpi = (float)LOWORD(wp);
                scale_ = dpi / 96.f;
                RECT* r = (RECT*)lp;
                SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left,
                             r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                destroy_target();   // DPI 变了要按新 DPI 重建渲染目标
                scene_.scale = scale_;
                invalidate();
                return 0;
            }

            case WM_SETTINGCHANGE: {
                // 用户切换了系统深浅色 / 强调色
                if (lp && wcscmp(L"ImmersiveColorSet", (const wchar_t*)lp) == 0) {
                    rebuild_palette();
                    apply_dwm();
                    if (backdrop_ != Backdrop::None) backdrop_supported_ = apply_backdrop();
                    for (auto& pp : popups_) if (pp) pp->set_palette(scene_.palette);
                    invalidate();
                }
                return 0;
            }

            case WM_CLOSE:
                // 关窗口时先把弹层收掉：弹层是独立的顶层窗口，带着它一起销毁容易在
                // 弹层的 WndProc 里读到已析构的状态（表现为退出时 0xC000041D 崩一下）
                for (auto& pp : popups_) if (pp) pp->hide_now(false);
                DestroyWindow(hwnd_);
                return 0;

            case WM_DESTROY:
                KillTimer(hwnd_, 1);
                KillTimer(hwnd_, 3);
                PostQuitMessage(0);
                return 0;

            default:
                break;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Window* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = (CREATESTRUCTW*)lp;
            self = (Window*)cs->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
            if (self) self->hwnd_ = h;
        } else {
            self = (Window*)GetWindowLongPtrW(h, GWLP_USERDATA);
        }
        if (self) return self->handle(msg, wp, lp);
        return DefWindowProcW(h, msg, wp, lp);
    }

    // -- 状态 --
    HWND  hwnd_ = nullptr;
    std::wstring title_;
    // 窗口外壳（标题栏 / 材质）
    Chrome   chrome_mode_ = Chrome::System;   // 默认系统栏，什么都不变
    float    title_h_ = 32.f;                 // 自绘栏高度（逻辑像素）
    std::wstring title_text_;                 // 空 → 用窗口标题
    bool     show_min_ = true, show_max_ = true, show_close_ = true;
    Backdrop backdrop_ = Backdrop::None;
    bool     backdrop_dark_tint_ = true;
    bool     backdrop_supported_ = false;     // 系统到底接没接（探测结果）
    bool     surface_acrylic_ = false;        // 客户区自己铺的玻璃底（默认关）
    int      h_chrome_btn_ = -1;              // 悬停的标题栏按钮：0 最小化 / 1 最大化 / 2 关闭
    int      down_chrome_btn_ = -1;           // 按下中的那个
    float width_ = 960.f, height_ = 640.f;
    Theme theme_ = Theme::Auto;
    uint32_t accent_ = 0;
    float padding_ = 24.f, spacing_ = 14.f, max_width_ = NAN;
    float scale_ = 1.f;
    bool  cursor_hand_ = false;

    Scene scene_;
    Com<ID2D1HwndRenderTarget> rt_;
    Painter painter_;
    std::vector<std::unique_ptr<PopupWin>> popups_;
    bool   popup_hover_open_ = true;      // 这套弹出层的默认：悬浮自动展开子菜单
    double popup_hover_delay_ = 0.0;      // 悬停多少毫秒后展开（0 = 停上去立刻出子菜单）
    int    popup_click_level_ = -1;       // 主窗口代收的弹层点击：目标层
    Point  popup_click_pt_{};             // 主窗口代收的弹层点击：客户区逻辑坐标
    bool headless_ = false;   // 自测模式：弹层只建窗口、不显示
    std::function<void()> on_ready_;
    // 窗口显示、DPI 与材质都落定之后调一次（run() 里）。启动就要弹浮层的程序挂这里，
    // 因为 run() 之前窗口还没建，那时 show_popup 拿不到 owner。
    std::function<void()> on_shown_;
    static constexpr UINT kPostTimerBase = 200;   // post() 用的定时器 id 从这里起
    std::map<UINT, std::function<void()>> delayed_;
    UINT next_timer_ = 0;
    bool ready_fired_ = false;
};

// ------------------------------------------------ Scene 弹出层方法实现 ----
// 放在 Window 之后，因为要用到完整的 Window 类型。
inline Rect Scene::client_screen_rect() const {
    if (!window || !window->hwnd()) return client_rect();
    RECT rc;
    GetClientRect(window->hwnd(), &rc);
    POINT tl{0, 0};
    ClientToScreen(window->hwnd(), &tl);
    float w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
    // 自绘标题栏：内容坐标是从标题栏下面开始的，弹层锚点要跟着下移 title_h，
    // 否则下拉框/菜单会整体高出目标行一条标题栏
    float top = 0.f;
    if (window->chrome() == Window::Chrome::Custom) top = window->title_height();
    return Rect((float)tl.x, (float)tl.y + top, w, h - top);
}

inline Rect Scene::monitor_work_rect() const {
    Rect fallback(0.f, 0.f, 1920.f, 1080.f);
    if (!window || !window->hwnd()) return fallback;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(window->hwnd(), MONITOR_DEFAULTTONEAREST);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return fallback;
    return Rect((float)mi.rcWork.left, (float)mi.rcWork.top,
                (float)(mi.rcWork.right - mi.rcWork.left),
                (float)(mi.rcWork.bottom - mi.rcWork.top));
}

inline bool Scene::show_popup(std::shared_ptr<PopupPanel> panel, const Rect& screen_px,
                              PopupDir dir) {
    return window ? window->show_popup(std::move(panel), screen_px, dir) : false;
}

inline bool Scene::open_submenu(std::shared_ptr<PopupPanel> panel, int row) {
    // 子菜单开在"点它的那一层"右边；拿不到发起者（比如程序直接调）才退回当前最深一层
    if (!window) return false;
    int lvl = take_submenu_source();
    if (lvl < 0 || lvl >= window->popup_depth()) lvl = window->popup_depth() - 1;
    return window->show_submenu(std::move(panel), lvl, row);
}

inline void Scene::close_popup() {
    if (window) window->close_popup();
}

inline bool Scene::popup_visible() const { return window && window->popup_visible(); }

inline void Scene::set_popup_hover(bool on, double delay_ms) {
    if (window) window->set_popup_hover(on, delay_ms);
}

}  // namespace w11ui
