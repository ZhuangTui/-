// w11ui / snapshot.hpp --------------------------------------------------------
// 离屏渲染：把控件树画进 WIC 位图再存 PNG。用于生成文档/预览图，
// 也可以在没有窗口的情况下做 CI 视觉回归。
#pragma once

#include "core.hpp"
#include "theme.hpp"
#include "painter.hpp"
#include "widgets.hpp"

namespace w11ui {

namespace detail {
struct SilentHost : Host {
    void request_repaint() override {}
    void mark_animating() override {}
    void fire(const std::string&, Widget*, bool) override {}
    void want_hand_cursor(bool) override {}
};
}  // namespace detail

// 只做一次布局，把控件的 bounds 算出来，不落盘。
// 需要"按某个控件的实际位置把弹层叠上去"时，先调它，再拿 bounds 去算弹层矩形。
inline bool layout_tree(std::shared_ptr<Widget> root, const Palette& pal, float scale,
                        float logical_w, float logical_h) {
    if (!root) return false;
    IWICImagingFactory* wic = wic_factory();
    ID2D1Factory* fac = d2d_factory();
    if (!wic || !fac) return false;
    UINT pw = (UINT)std::lround(logical_w * scale);
    UINT ph = (UINT)std::lround(logical_h * scale);
    if (pw == 0 || ph == 0) return false;

    Com<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 bitmap.put())))
        return false;
    D2D1_RENDER_TARGET_PROPERTIES props{};
    props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    props.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    props.dpiX = props.dpiY = 96.f * scale;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    Com<ID2D1RenderTarget> rt;
    if (FAILED(fac->CreateWicBitmapRenderTarget(bitmap.get(), props, rt.put()))) return false;

    detail::SilentHost host;
    Host* prev = root->host;
    root->attach(&host);
    Painter p;
    if (!p.init(rt.get(), pal, scale, false)) { root->host = prev; return false; }
    root->arrange(p, Rect(0.f, 0.f, logical_w, logical_h));
    root->host = prev;
    return true;
}

// logical_w/h 为逻辑尺寸（DIP），scale 为像素倍率：输出 = logical * scale。
// 例：1280x720 @ 1.5 => 1920x1080。
// overlay / overlay_rect 用于把「弹出层」这类不属于控件树的东西叠在最后画（下拉框截图）。

// 一次叠多层（比如"4 级级联菜单"的全展开示意图），每层给自己那块矩形
inline bool render_to_png_multi(
    std::shared_ptr<Widget>, const Palette&, float, float, float, const std::wstring&,
    const std::vector<std::pair<std::shared_ptr<Widget>, Rect>>&, float,
    std::function<void(Painter&, float)>, bool);

inline bool render_to_png(std::shared_ptr<Widget> root, const Palette& pal, float logical_w,
                          float logical_h, float scale, const std::wstring& path,
                          std::shared_ptr<Widget> overlay = nullptr,
                          const Rect& overlay_rect = Rect(), float content_top = 0.f,
                          std::function<void(Painter&, float)> top_draw = {},
                          bool glass_bg = false) {
    std::vector<std::pair<std::shared_ptr<Widget>, Rect>> one;
    if (overlay) one.emplace_back(overlay, overlay_rect);
    return render_to_png_multi(root, pal, logical_w, logical_h, scale, path, one, content_top,
                               std::move(top_draw), glass_bg);
}

// 一次叠多层（比如"4 级级联菜单"的全展开示意图），每层给自己那块矩形
// content_top / top_draw：自绘标题栏时内容整体下移，最后再把那条栏画上去
// glass_bg：底层也铺玻璃渐变（出"背景亚克力"的图）
inline bool render_to_png_multi(
    std::shared_ptr<Widget> root, const Palette& pal, float logical_w, float logical_h,
    float scale, const std::wstring& path,
    const std::vector<std::pair<std::shared_ptr<Widget>, Rect>>& overlays,
    float content_top = 0.f, std::function<void(Painter&, float)> top_draw = {},
    bool glass_bg = false) {
    if (!root) return false;
    IWICImagingFactory* wic = wic_factory();
    ID2D1Factory* fac = d2d_factory();
    if (!wic || !fac) return false;

    UINT pw = (UINT)std::lround(logical_w * scale);
    UINT ph = (UINT)std::lround(logical_h * scale);
    if (pw == 0 || ph == 0) return false;

    Com<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA,
                                 WICBitmapCacheOnLoad, bitmap.put())))
        return false;

    D2D1_RENDER_TARGET_PROPERTIES props{};
    props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    props.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM,
                                          D2D1_ALPHA_MODE_PREMULTIPLIED};
    props.dpiX = props.dpiY = 96.f * scale;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    Com<ID2D1RenderTarget> rt;
    if (FAILED(fac->CreateWicBitmapRenderTarget(bitmap.get(), props, rt.put()))) return false;

    detail::SilentHost host;
    // 换 host 只为了离屏绘制，画完必须换回去 —— 否则控件里的 host 指向栈上对象，
    // 之后任何 request_repaint() 都是野指针（表现是进程在回调里直接崩掉）。
    Host* prev_root_host = root->host;
    root->attach(&host);

    Painter p;
    if (!p.init(rt.get(), pal, scale, true)) { root->host = prev_root_host; return false; }
    root->arrange(p, Rect(0.f, content_top, logical_w, logical_h - content_top));

    p.begin(pal.window_bg);
    if (glass_bg) p.fill_surface_glass(Rect(0.f, 0.f, logical_w, logical_h), pal.dark);
    root->paint(p);
    for (auto& ov : overlays) {
        if (!ov.first) continue;
        Host* prev = ov.first->host;
        ov.first->attach(&host);
        ov.first->arrange(p, ov.second);
        ov.first->paint(p);
        ov.first->host = prev;
    }
    if (top_draw) top_draw(p, logical_w);   // 自绘标题栏最后画（盖在内容上面）
    root->host = prev_root_host;
    if (FAILED(p.end())) return false;

    // 编码 PNG
    Com<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(conv.put()))) return false;
    if (FAILED(conv->Initialize(bitmap.get(), GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom)))
        return false;

    Com<IWICStream> stream;
    if (FAILED(wic->CreateStream(stream.put()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;

    Com<IWICBitmapEncoder> enc;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.put()))) return false;
    if (FAILED(enc->Initialize(stream.get(), WICBitmapEncoderNoCache))) return false;

    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> bag;
    if (FAILED(enc->CreateNewFrame(frame.put(), bag.put()))) return false;
    if (FAILED(frame->Initialize(bag.get()))) return false;

    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetSize(pw, ph);
    frame->SetPixelFormat(&fmt);
    frame->SetResolution(96.0 * scale, 96.0 * scale);
    if (FAILED(frame->WriteSource(conv.get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    if (FAILED(enc->Commit())) return false;
    return true;
}

// 把一段 32bpp 预乘 BGRA 内存写成 PNG（分层窗口的像素检查用）
inline bool save_bgra_png(const void* px, int w, int h, const std::wstring& path) {
    IWICImagingFactory* wic = wic_factory();
    if (!wic || !px || w <= 0 || h <= 0) return false;

    Com<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmapFromMemory((UINT)w, (UINT)h, GUID_WICPixelFormat32bppPBGRA,
                                           (UINT)(w * 4), (UINT)(w * 4 * h), (BYTE*)px,
                                           bitmap.put())))
        return false;

    Com<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(conv.put()))) return false;
    if (FAILED(conv->Initialize(bitmap.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;

    Com<IWICStream> stream;
    if (FAILED(wic->CreateStream(stream.put()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;

    Com<IWICBitmapEncoder> enc;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.put()))) return false;
    if (FAILED(enc->Initialize(stream.get(), WICBitmapEncoderNoCache))) return false;

    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> bag;
    if (FAILED(enc->CreateNewFrame(frame.put(), bag.put()))) return false;
    if (FAILED(frame->Initialize(bag.get()))) return false;

    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetSize((UINT)w, (UINT)h);
    frame->SetPixelFormat(&fmt);
    frame->SetResolution(96.0, 96.0);
    if (FAILED(frame->WriteSource(conv.get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    if (FAILED(enc->Commit())) return false;
    return true;
}

}  // namespace w11ui
