// ============================================================ error.hpp ====
// 出错也要能跑起来：UI 没能正确加载时，不闪退、不黑屏，开一个报错窗口把
// "哪里错了 / 文件:行 / 怎么改"列出来；用 code.environment.set(true/false) 控制
// 报错窗口显示详细内容还是只显示一行 "UI加载错误"（防止把源码/路径泄露给别人）。
//
//   w11ui::code_environment(true);    // true  / "true" / "TRUE" / "tRuE" 都行
//   w11ui::code_environment(false);   // false / "false" / "False"
//
//   int main() {
//       w11ui::code_environment(arg_code_env(argc, argv));   // --code-env true|false
//       return w11ui::run_guarded([&] { return build().run(); });
//   }
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstring>

#include "core.hpp"
#include "theme.hpp"
#include "painter.hpp"
#include "kwargs.hpp"
#include "widgets.hpp"
#include "window.hpp"

namespace w11ui {

// ------------------------------------------------------------ UiError ----
// 库内部只抛这一种错误：消息 + 修复建议 + 抛出的位置（__FILE__ / __LINE__）
struct UiError {
    std::wstring message;   // 哪里错了
    std::wstring fix;       // 应该怎么改
    std::wstring file;      // 出错的文件
    int          line = 0;  // 出错的行号

    UiError() = default;
    UiError(std::wstring m, std::wstring f = std::wstring(), std::wstring file_ = std::wstring(),
            int ln = 0)
        : message(std::move(m)), fix(std::move(f)), file(std::move(file_)), line(ln) {}
};

// 抛错并记录位置：W11UI_FAIL(L"下拉框没有选项", L"用 kw::items({...}) 传进去")
#define W11UI_FAIL(msg, fix) \
    throw ::w11ui::UiError(std::wstring(msg), std::wstring(fix), std::wstring(__FILE__), __LINE__)

// ------------------------------------------------- code.environment.set ----
inline bool& code_env_slot() {
    static bool on = true;
    return on;
}

// 宽松解析：忽略大小写与前后的空白，t/T/1/y/Y 开头算真，其余算假
inline bool parse_switch(const std::wstring& v) {
    size_t i = 0;
    while (i < v.size() && (v[i] == L' ' || v[i] == L'\t' || v[i] == L'"' || v[i] == L'\'')) ++i;
    if (i >= v.size()) return false;
    wchar_t c = v[i];
    if (c == L't' || c == L'T' || c == L'1' || c == L'y' || c == L'Y') return true;
    if (c == L'f' || c == L'F' || c == L'0' || c == L'n' || c == L'N') return false;
    return false;
}
inline bool parse_switch(const std::string& v) {
    return parse_switch(to_wide(v));
}

// code.environment.set(true/false)
inline void code_environment(bool on) { code_env_slot() = on; }
inline void code_environment(const std::wstring& v) { code_env_slot() = parse_switch(v); }
inline void code_environment(const wchar_t* v) { code_env_slot() = parse_switch(std::wstring(v ? v : L"")); }
inline void code_environment(const std::string& v) { code_env_slot() = parse_switch(v); }
inline void code_environment(const char* v) { code_env_slot() = parse_switch(std::string(v ? v : "")); }
inline bool code_environment() { return code_env_slot(); }

// 同一个开关的长名字（"code.environment.set"）与简写都指向它
inline void code_environment_set(bool on) { code_environment(on); }
inline void code_environment_set(const char* v) { code_environment(v); }
inline void code_environment_set(const std::string& v) { code_environment(v); }
inline void code_environment_set(const wchar_t* v) { code_environment(v); }
inline bool code_environment_get() { return code_environment(); }

// 从命令行读 --code-env true / --code-env=false（没写就返回 true，即"开发时显示详情"）
inline bool arg_code_env(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i] ? argv[i] : "";
        const std::string k1 = "--code-env=", k2 = "--code-env";
        if (a.rfind(k1, 0) == 0) return parse_switch(a.substr(k1.size()));
        if (a == k2 && i + 1 < argc) return parse_switch(std::string(argv[i + 1] ? argv[i + 1] : ""));
    }
    return true;
}

// ------------------------------------------------------- 报错文本 ----
// 报错窗口/报告里出现的所有文案都集中在这里。库只给一套默认（中文），
// 应用可以随时整体替换（本地化、换措辞、换按钮字），不用改库源码：
//     w11ui::ErrorTexts t; t.close_btn = L"Close"; ... ; w11ui::set_error_texts(t);
struct ErrorTexts {
    std::wstring title       = L"UI 加载错误";   // 窗口标题
    std::wstring short_line  = L"UI加载错误";    // 不显示详情时唯一的一行
    std::wstring heading     = L"UI 加载错误";   // 详情页大标题
    std::wstring lead        = L"界面没能加载出来，程序还活着。下面是可以直接照着改的信息。";
    std::wstring problem     = L"问题";          // 详情页小节标题
    std::wstring unknown     = L"未知错误";
    std::wstring where       = L"位置";          // 报告文本里的前缀
    std::wstring where_inline = L"位置　";        // 详情页里"位置　文件名"（含间隔）
    std::wstring fix_label   = L"怎么改";        // 详情页小节标题
    std::wstring fix_report  = L"修复建议";      // 报告文本里的前缀
    std::wstring label_sep   = L"：";            // 前缀与内容之间的分隔符（英文体系下换 ":"）
    std::wstring copy_btn    = L"复制详情";
    std::wstring close_btn   = L"关闭";
    // run_guarded 包住的异常，用它兜底成 UiError 时的措辞
    std::wstring runtime_prefix = L"运行时异常：";
    std::wstring runtime_fix    = L"检查这一处用到的控件参数（类型/数量/取值）是否正确；"
                                  L"上面这行是 C++ 运行时抛出的原始信息。";
    std::wstring unknown_exc    = L"未知异常（不是 std::exception 派生的）";
    std::wstring unknown_exc_fix = L"在可疑的调用外面套一层 try/catch，把异常类型打出来。";
};
inline ErrorTexts& error_texts() { static ErrorTexts t; return t; }
inline void set_error_texts(const ErrorTexts& t) { error_texts() = t; }
inline void reset_error_texts() { error_texts() = ErrorTexts{}; }

inline std::wstring file_stem(const std::wstring& path) {
    size_t s = path.find_last_of(L"/\\");
    return s == std::wstring::npos ? path : path.substr(s + 1);
}

// full=true：完整详情；full=false：只有一行（不露源码/路径）
inline std::wstring error_report(const UiError& e, bool full) {
    const ErrorTexts& T = error_texts();
    if (!full) return T.short_line;
    std::wstring t;
    t += T.heading + L"\n\n";
    t += T.problem + T.label_sep + (e.message.empty() ? T.unknown : e.message) + L"\n";
    if (!e.file.empty()) {
        t += T.where + T.label_sep + file_stem(e.file);
        if (e.line > 0) t += L":" + std::to_wstring(e.line);
        t += L"\n";
    }
    if (!e.fix.empty()) t += L"\n" + T.fix_report + T.label_sep + L"\n" + e.fix;
    return t;
}

// -------------------------------------------------------- 报错窗口 ----
// 一个独立的小窗口：标题用 error_texts().title，正文按开关决定详细还是只留一行。
inline int show_error_window(const UiError& e, Theme th = Theme::Dark) {
    ensure_com();
    bool full = code_environment();
    const ErrorTexts& T = error_texts();

    int rw = full ? 780.f : 420.f;
    int rh = full ? 520.f : 260.f;

    Window w(kw::title(T.title), kw::width((float)rw), kw::height((float)rh),
             kw::theme(th), kw::padding(26.f), kw::spacing(10.f));

    if (!full) {
        // 只有这一行：不显示任何源码、路径、行号
        w.add(ui::label(kw::text(T.short_line), kw::scale(TextScale::Title)));
        w.on("ok", [](Event&) { PostQuitMessage(0); });
        w.add(ui::button(kw::text(T.close_btn), kw::click("ok")));
        return w.run();
    }

    w.add(ui::label(kw::text(T.heading), kw::scale(TextScale::Title)));
    w.add(ui::label(kw::text(T.lead), kw::role(Role::Secondary), kw::wrap(true)));
    w.add(ui::divider());

    w.add(ui::label(kw::text(T.problem), kw::role(Role::Secondary)));
    w.add(ui::label(kw::text(e.message.empty() ? T.unknown : e.message), kw::wrap(true)));
    if (!e.file.empty()) {
        std::wstring loc = T.where_inline + file_stem(e.file);
        if (e.line > 0) loc += L":" + std::to_wstring(e.line);
        w.add(ui::label(kw::text(loc), kw::role(Role::Secondary), kw::wrap(true)));
    }
    if (!e.fix.empty()) {
        w.add(ui::label(kw::text(T.fix_label), kw::role(Role::Secondary)));
        w.add(ui::label(kw::text(e.fix), kw::wrap(true)));
    }

    w.on("copy", [e](Event&) {
        std::wstring txt = error_report(e, true);
        if (!OpenClipboard(nullptr)) return;
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (txt.size() + 1) * sizeof(wchar_t));
        if (h) {
            void* p = GlobalLock(h);
            if (p) {
                std::memcpy(p, txt.c_str(), (txt.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
        }
        CloseClipboard();
    });
    w.on("ok", [](Event&) { PostQuitMessage(0); });

    auto row = std::make_shared<Stack>(kw::spacing(8.f));
    row->add(ui::button(kw::text(T.copy_btn), kw::click("copy")));
    row->add(ui::button(kw::text(T.close_btn), kw::click("ok"), kw::variant(Variant::Primary)));
    w.add(row);
    return w.run();
}

// ------------------------------------------------- rank: run_guarded ----
// 把整个 app 包起来：任何 UiError / std::exception / 未知异常都变成报错窗口，
// 而不是直接崩掉（"格式有错也要能跑起来"）。
inline int run_guarded(const std::function<int()>& body, Theme th = Theme::Dark) {
    const ErrorTexts& T = error_texts();
    try {
        return body();
    } catch (const UiError& e) {
        return show_error_window(e, th);
    } catch (const std::exception& ex) {
        UiError e;
        e.message = T.runtime_prefix + to_wide(std::string(ex.what() ? ex.what() : ""));
        e.fix = T.runtime_fix;
        return show_error_window(e, th);
    } catch (...) {
        UiError e;
        e.message = T.unknown_exc;
        e.fix = T.unknown_exc_fix;
        return show_error_window(e, th);
    }
}

} // namespace w11ui
