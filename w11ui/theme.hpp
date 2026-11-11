// w11ui / theme.hpp -----------------------------------------------------------
// Win11 设计令牌（Design Token）：颜色表、深浅色检测、系统强调色读取。
#pragma once

#include "core.hpp"

namespace w11ui {

// 一套完整的语义色板。所有控件只引用语义角色，不写死颜色，
// 因此切换浅色/深色时整棵树自动跟着变。
struct Palette {
    bool  dark = false;
    Color window_bg;        // 窗口底色
    Color layer;            // 卡片下方的次级层
    Color card;             // 卡片面
    Color card_hover;
    Color card_press;
    Color control;          // 标准按钮面
    Color control_hover;
    Color control_press;
    Color control_stroke;
    Color stroke;           // 卡片描边
    Color stroke_soft;
    Color text;             // 主文本
    Color text_secondary;   // 次文本
    Color text_tertiary;    // 说明文本
    Color accent;           // 系统强调色
    Color accent_hover;
    Color accent_press;
    Color on_accent;        // 强调色上的文字
    Color focus;            // 焦点环
    Color shadow;
    Color success;
    Color danger;
    Color divider;
    Color flyout;           // 弹出层（菜单/下拉面板）面
    Color flyout_hover;     // 菜单行的悬浮底色（比卡片 hover 明显一点）
    Color flyout_sep;       // 菜单分隔线（要能隔着一段距离看见）
    Color flyout_border;    // 浮层描边：**必须是深色**，否则父子面板贴一起就看不出边界
    Color track;            // 进度条的槽（要比卡片面亮/暗一档，不然"还剩多少"看不出来）
    Color field;            // 输入框面
    Color field_line;       // 输入框底部那条线（未聚焦）
    Color scrim;            // 模态弹窗的遮罩

    Color text_for(Role r) const {
        switch (r) {
            case Role::Secondary: return text_secondary;
            case Role::Accent:    return accent;
            case Role::Disabled:  return text_tertiary;
            default:              return text;
        }
    }
};

// ------------------------------------------------------ 系统主题/强调色 ----
inline bool read_dword(const wchar_t* subkey, const wchar_t* name, uint32_t& out) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    DWORD v = 0, size = sizeof(v), type = 0;
    bool ok = RegQueryValueExW(h, name, nullptr, &type, (LPBYTE)&v, &size) == ERROR_SUCCESS &&
              type == REG_DWORD;
    RegCloseKey(h);
    if (ok) out = (uint32_t)v;
    return ok;
}

inline bool system_is_light() {
    uint32_t v = 1;
    if (read_dword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", v)) {
        return v != 0;
    }
    return true;  // 读不到就按浅色处理
}

inline uint32_t read_accent_hex() {
    // HKCU\...\DWM\AccentColor 是 0xAABBGGRR
    uint32_t abgr = 0;
    if (read_dword(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", abgr)) {
        uint32_t rgb = ((abgr & 0xFF) << 16) | (((abgr >> 8) & 0xFF) << 8) | ((abgr >> 16) & 0xFF);
        if ((rgb & 0xFFFFFF) != 0) return rgb & 0xFFFFFF;
    }
    return 0x0067C0;  // Win11 浅色默认强调色
}

// 强调色不是直接照搬，而是按 Win11 的强调色梯度生成 hover / press。
inline Palette make_palette(bool dark, uint32_t accent_rgb = 0) {
    Palette p;
    p.dark = dark;
    Color accent = Color::hex(accent_rgb ? accent_rgb : read_accent_hex());

    if (!dark) {
        p.window_bg       = Color::hex(0xF3F3F3);
        p.layer           = Color::hex(0xEDEDED);
        p.card            = Color::hex(0xFFFFFF);
        p.card_hover      = Color::hex(0xFBFAFA);
        p.card_press      = Color::hex(0xF4F3F3);
        p.control         = Color::hex(0xFDFDFD);
        p.control_hover   = Color::hex(0xF7F7F7);
        p.control_press   = Color::hex(0xEFEFEF);
        p.control_stroke  = Color::hex(0x000000, 0.06f);
        p.stroke          = Color::hex(0x000000, 0.07f);
        p.stroke_soft     = Color::hex(0x000000, 0.04f);
        p.text            = Color::hex(0x1A1A1A);
        p.text_secondary  = Color::hex(0x5C5C5C);
        p.text_tertiary   = Color::hex(0x8A8A8A);
        p.accent          = darken(accent, 0.06f);
        p.accent_hover    = darken(accent, 0.14f);
        p.accent_press    = darken(accent, 0.26f);
        // 浅色下强调色按钮文字：亮色用黑，深色用白
        float lum = 0.299f * accent.r + 0.587f * accent.g + 0.114f * accent.b;
        p.on_accent       = lum > 0.62f ? Color::hex(0x101010) : Color(1.f, 1.f, 1.f);
        p.focus           = Color::hex(0x000000, 0.12f);
        p.shadow          = Color::hex(0x000000, 0.22f);
        p.success         = Color::hex(0x0F7B0F);
        p.danger          = Color::hex(0xC42B1C);
        p.divider         = Color::hex(0x000000, 0.06f);
        p.flyout          = Color::hex(0xFCFCFC);
        p.flyout_hover    = Color::hex(0xF0F0F0);
        p.flyout_sep      = Color::hex(0x000000, 0.55f);   // 浅色：3 分米外也要能看见
        p.flyout_border   = Color::hex(0x000000, 0.16f);   // 浅色浮层描边（#FCFCFC 上约 #D8D8D8）
        p.track           = Color::hex(0x000000, 0.10f);   // 白卡上压到 ≈#E6E6E6
        p.field           = Color::hex(0xFBFBFB, 1.00f);   // 白卡上的输入框：比卡略灰，不然白对白看不出
        p.field_line      = Color::hex(0x000000, 0.40f);   // 未聚焦再乘 0.5 透明度，还要看得见
        p.scrim           = Color::hex(0x000000, 0.40f);   // 遮罩终值：动画里从 0 淡到 0.40
    } else {
        p.window_bg       = Color::hex(0x202020);
        p.layer           = Color::hex(0x272727);
        p.card            = Color::hex(0x2C2C2C);
        p.card_hover      = Color::hex(0x313131);
        p.card_press      = Color::hex(0x363636);
        p.control         = Color::hex(0x2D2D2D);
        p.control_hover   = Color::hex(0x333333);
        p.control_press   = Color::hex(0x3A3A3A);
        p.control_stroke  = Color::hex(0xFFFFFF, 0.07f);
        p.stroke          = Color::hex(0xFFFFFF, 0.08f);
        p.stroke_soft     = Color::hex(0xFFFFFF, 0.05f);
        p.text            = Color::hex(0xE9E9E9);
        p.text_secondary  = Color::hex(0xA6A6A6);
        p.text_tertiary   = Color::hex(0x7A7A7A);
        // 深色主题下强调色要提亮，否则在深底上糊成一片
        p.accent          = lighten(accent, 0.42f);
        p.accent_hover    = lighten(accent, 0.52f);
        p.accent_press    = lighten(accent, 0.34f);
        p.on_accent       = Color::hex(0x0B0B0B);
        p.focus           = Color::hex(0xFFFFFF, 0.14f);
        p.shadow          = Color::hex(0x000000, 0.26f);
        p.success         = Color::hex(0x6CCB5F);
        p.danger          = Color::hex(0xFF99A4);
        p.divider         = Color::hex(0xFFFFFF, 0.08f);
        p.flyout          = Color::hex(0x303030);
        p.flyout_hover    = Color::hex(0x3C3C3C);
        p.flyout_sep      = Color::hex(0xFFFFFF, 0.68f);   // 深色：3 分米外也要能看见
        // 深色浮层描边：Edge 实测就是 #000 @20%（#303030 上压出来 ≈#262626，两条叠在缝上 ≈#252525）
        p.flyout_border   = Color::hex(0x000000, 0.20f);
        p.track           = Color::hex(0xFFFFFF, 0.10f);   // #2C2C2C 卡上压到 ≈#414141
        p.field           = Color::hex(0xFFFFFF, 0.06f);   // #2C2C2C 上压到 ≈#383838
        p.field_line      = Color::hex(0xFFFFFF, 0.40f);   // 未聚焦再乘 0.5 透明度，还要看得见
        p.scrim           = Color::hex(0x000000, 0.40f);   // 深浅两套都用 0.40（动画从 0 淡上来）
    }
    return p;
}

// 按钮/控件圆角：Win11 控件 4px，卡片 8px，浮层 8px，胶囊 999。
struct Radius {
    static constexpr float control = 4.f;
    static constexpr float card    = 8.f;
    static constexpr float flyout  = 8.f;
    static constexpr float pill    = 999.f;
};

// 动效时长：Win11 的 Fluent Motion 常用 150 / 200 / 250 / 300 ms。
struct Motion {
    static constexpr double fast    = 120.0;   // 悬停变色
    static constexpr double normal  = 200.0;   // 开关、状态切换
    static constexpr double slow    = 300.0;   // 入场
};

}  // namespace w11ui
