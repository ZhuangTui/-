// w11ui / kwargs.hpp -----------------------------------------------------------
// C++ 没有 Python 的具名参数，这里用「选项对象 + 折叠表达式」模拟：
//     w11ui::Window(kw::title("Demo"), kw::width(920), kw::height(600))
// 每个 kw::xxx() 返回一个带 apply(Opts&) 的小对象，ctor 里逐条应用。
// 好处：编译期类型检查、可以任意顺序、可省略，语义与 Python 的 **kwargs 一致。
#pragma once

#include "core.hpp"
#include "theme.hpp"
#include "painter.hpp"

namespace w11ui {

// 所有控件共享的一张「参数表」。控件只读取自己关心的字段，其余忽略，
// 于是同一套 kw:: 语法对 Window / Card / Button / Toggle 全部通用。
struct Opts {
    // 文本
    std::wstring title;
    std::wstring subtitle;
    std::wstring text;
    std::wstring icon;      // Segoe Fluent Icons 字形（UTF-16 码位）

    // 尺寸提示，NAN 表示由布局决定
    float width  = NAN;
    float height = NAN;
    float flex   = 0.f;     // >0 时在父容器里按权重分配剩余空间
    float padding  = NAN;
    float spacing  = NAN;
    float max_width = NAN;
    float radius   = NAN;
    float gap      = NAN;

    // 语义
    Theme      theme   = Theme::Auto;
    Variant    variant = Variant::Standard;
    TextScale  scale   = TextScale::Body;
    Role       role    = Role::Primary;
    HAlign     align   = HAlign::Start;
    uint32_t   accent  = 0;      // 0 = 跟随系统

    // 行为
    std::string click;      // 点击事件名 -> Window::on("name", handler)
    std::string change;     // 开关/数值变化事件名
    bool checked  = false;
    bool spinning = false;
    bool wrap     = false;
    bool stretch  = true;    // 子控件是否横向填满
    bool enabled  = true;

    // 外观
    int  shadow = -1;       // -1 = 控件用自己的默认（卡片/下拉框默认开，其它默认关）
                            // 0 = 显式关，1 = 显式开（kw::shadow(bool)）
    int  acrylic = -1;      // 磨砂面（亚克力的视觉近似）：-1 跟随控件默认（关），1 = kw::acrylic() 打开

    // 按钮形态（kw::as_toggle / as_repeat / as_hyperlink / as_split / as_dropdown）
    int  button_kind = 0;   // 0 普通 / 1 切换 / 2 重复 / 3 超链接 / 4 拆分 / 5 下拉
    bool toggled  = false;  // 切换按钮初始是否按下
    long repeat_ms = -1;    // 重复按钮：按住多久开始连发（-1 = 默认 400）
    std::function<void(int)> on_pick_index;   // 拆分/下拉按钮：选中第几项
    bool context = false;   // 右键菜单：把这个控件的 items 当右键菜单（kw::context()）

    // 图片（Image）
    std::wstring src;       // 图片路径（png / jpg / bmp）
    int  fit_mode = 0;      // 0 = cover（裁满，默认）/ 1 = contain（完整装入）

    // 星级 / 提示条 / 折叠面板
    int  rating = 0;        // 星级初始值
    int  rating_max = 5;    // 几颗星
    int  severity = 0;      // InfoBar：0 信息 / 1 成功 / 2 警告 / 3 错误
    bool closable = false;  // InfoBar：显示右上角关闭钮
    bool expanded = false;  // Expander：初始是否展开
    bool arrows = false;    // PipsPager：显示上一页 / 下一页箭头

    // 下拉框（ComboBox）
    std::vector<std::wstring> items;   // 选项列表（必填）
    std::wstring placeholder;          // 未选中时显示的提示
    int   selected = -1;               // 初始选中项，-1 = 不选
    bool  allow_overflow = false;      // 弹出层能否超出窗口（默认否，只在窗口内）
    bool  menu = false;                // 菜单式下拉：只触发动作，不回填内容
    bool  menubar = false;             // 菜单栏式触发："文件(F)"那种——只有文字，没有框和箭头
    bool  open = false;                // 初始是否展开
    int   hover_open = -1;             // 子菜单悬浮是否自动展开：-1 跟随默认（开）, 0 关, 1 开
    float hover_delay = NAN;           // 悬浮多少毫秒后展开（默认 0 = 立刻）
    float max_height = NAN;            // 弹出层最大高度
    float row_height = NAN;            // 每行高度

    // 进度条（ProgressBar）
    float progress    = NAN;           // 0..1；NAN = 用控件默认
    bool  indeterminate = false;       // 不定态（一条游标来回扫）
    float thickness   = NAN;           // 条的粗细（默认 4）

    // 输入框（TextBox）
    bool  password   = false;          // 密码模式：字符显示成圆点
    int   max_length = -1;             // 最多几个字符（-1 = 不限）
    bool  readonly   = false;          // 只读（可选中、可复制，不能改）

    // 弹窗（Dialog）
    std::wstring ok_text;              // 主按钮文字（空 = 不画）
    std::wstring cancel_text;          // 次按钮文字（空 = 不画）
    float min_width  = NAN;            // 面板最小宽度
    bool  close_on_scrim = true;       // 点遮罩关掉
    bool  draggable  = false;          // 按住面板拖动（**默认关**：弹窗默认钉在中央不动）

    // 数字框（NumberBox）
    double num_value  = 0.0;            // 初始值
    double num_min    = -1e9;           // 下限
    double num_max    = 1e9;            // 上限
    double num_step   = 1.0;            // 一次步进的量
    int    num_decimals = 0;            // 小数位（0 = 整数）
    std::wstring suffix;                // 单位后缀（"px" / "%" / "ms"），空 = 不画
    int    num_style  = 0;              // 0 = 框内上下箭头 / 1 = 左右 − + 按钮 / 2 = 不画按钮
    long   repeat_delay_ms = -1;        // 按住多久开始连发（-1 = 默认 400）
    std::function<void(double)> on_value;   // 值变化回调

    // 日期选择器（DatePicker）
    std::wstring date_format = L"yyyy-MM-dd";  // 回填格式：yyyy / yy / MM / M / dd / d / MMM，默认 4y-2m-2d

    // 时间面板（TimePicker）
    int  hour = 0, minute = 0, second = 0;
    bool show_seconds = false;
    std::wstring time_format = L"HH:mm";       // 回填格式：HH / H / mm / ss / HH:mm[:ss]
    std::function<void(int, int, int)> on_pick_time;
    std::vector<std::wstring> week_names;      // 7 个（周日..周六）；空 = 库给英文缩写
    std::vector<std::wstring> month_names;     // 12 个（一月..十二月）；空 = 用数字
    std::wstring title_pattern;                // 标题格式，空 = L"yyyy-MM"
    std::wstring clear_text;                   // 左下角文字（空 = 不画该按钮）
    std::wstring today_text;                   // 右下角文字（空 = 不画该按钮）
    int  week_start = 1;                       // 表头从周几起：0 = 周日，1 = 周一
    int  year = 0, month = 0, day = 0;         // 初始日期（0 = 用系统今天）
    std::function<void(int, int, int)> on_pick_date;   // 选完回调；清除时是 (0,0,0)

    // 取色器（ColorPicker）
    std::wstring color_format;                 // 回填格式，空 = L"#{hex}"（见 format_color 的令牌表）
    int  color = -1;                           // 初始色 0xRRGGBB（-1 = 没给，交给程序/输入框）
    std::vector<unsigned> swatches;            // 预设色板 0xRRGGBB（空 = 不画这一行）
    std::function<void(int, int, int)> on_pick_color;  // 确定/点色板给 (r,g,b)
};

// ------------------------------------------------------------------ kw ----
namespace kw {

struct width_o   { float v; void operator()(Opts& o) const { o.width = v; } };
struct height_o  { float v; void operator()(Opts& o) const { o.height = v; } };
struct flex_o    { float v; void operator()(Opts& o) const { o.flex = v; } };
struct padding_o { float v; void operator()(Opts& o) const { o.padding = v; } };
struct spacing_o { float v; void operator()(Opts& o) const { o.spacing = v; } };
struct gap_o     { float v; void operator()(Opts& o) const { o.gap = v; } };
struct maxwidth_o{ float v; void operator()(Opts& o) const { o.max_width = v; } };
struct radius_o  { float v; void operator()(Opts& o) const { o.radius = v; } };

struct title_o    { std::wstring v; void operator()(Opts& o) const { o.title = v; } };
struct subtitle_o { std::wstring v; void operator()(Opts& o) const { o.subtitle = v; } };
struct text_o     { std::wstring v; void operator()(Opts& o) const { o.text = v; } };
struct icon_o     { std::wstring v; void operator()(Opts& o) const { o.icon = v; } };

struct theme_o   { Theme v;   void operator()(Opts& o) const { o.theme = v; } };
struct variant_o { Variant v; void operator()(Opts& o) const { o.variant = v; } };
struct scale_o   { TextScale v; void operator()(Opts& o) const { o.scale = v; } };
struct role_o    { Role v;    void operator()(Opts& o) const { o.role = v; } };
struct align_o   { HAlign v;  void operator()(Opts& o) const { o.align = v; } };
struct accent_o  { uint32_t v; void operator()(Opts& o) const { o.accent = v; } };

struct click_o  { std::string v; void operator()(Opts& o) const { o.click = v; } };
struct change_o { std::string v; void operator()(Opts& o) const { o.change = v; } };
struct checked_o{ bool v; void operator()(Opts& o) const { o.checked = v; } };
struct spinning_o { bool v; void operator()(Opts& o) const { o.spinning = v; } };
struct wrap_o   { bool v; void operator()(Opts& o) const { o.wrap = v; } };
struct stretch_o{ bool v; void operator()(Opts& o) const { o.stretch = v; } };
struct enabled_o{ bool v; void operator()(Opts& o) const { o.enabled = v; } };
struct shadow_o { bool v; void operator()(Opts& o) const { o.shadow = v ? 1 : 0; } };
struct acrylic_o { bool v; void operator()(Opts& o) const { o.acrylic = v ? 1 : 0; } };

// 下拉框
struct items_o {
    std::vector<std::wstring> v;
    void operator()(Opts& o) const { o.items = v; }
};
struct placeholder_o { std::wstring v; void operator()(Opts& o) const { o.placeholder = v; } };
struct selected_o    { int v;  void operator()(Opts& o) const { o.selected = v; } };
struct overflow_o    { bool v; void operator()(Opts& o) const { o.allow_overflow = v; } };
struct menu_o        { bool v; void operator()(Opts& o) const { o.menu = v; } };
struct menubar_o     { bool v; void operator()(Opts& o) const { o.menubar = v; } };
struct hoveropen_o   { bool v; void operator()(Opts& o) const { o.hover_open = v ? 1 : 0; } };
struct hoverdelay_o  { float v; void operator()(Opts& o) const { o.hover_delay = v; } };
struct open_o        { bool v; void operator()(Opts& o) const { o.open = v; } };
struct maxheight_o   { float v; void operator()(Opts& o) const { o.max_height = v; } };
struct rowheight_o   { float v; void operator()(Opts& o) const { o.row_height = v; } };
struct value_o       { std::wstring v; void operator()(Opts& o) const { o.text = v; } };

// 数字框
struct num_o       { double v; void operator()(Opts& o) const { o.num_value = v; } };
struct nummin_o    { double v; void operator()(Opts& o) const { o.num_min = v; } };
struct nummax_o    { double v; void operator()(Opts& o) const { o.num_max = v; } };
struct numstep_o   { double v; void operator()(Opts& o) const { o.num_step = v; } };
struct decimals_o  { int v;    void operator()(Opts& o) const { o.num_decimals = v; } };
struct suffix_o    { std::wstring v; void operator()(Opts& o) const { o.suffix = v; } };
struct numstyle_o  { int v;    void operator()(Opts& o) const { o.num_style = v; } };
struct repeatdelay_o { long v; void operator()(Opts& o) const { o.repeat_delay_ms = v; } };
struct onvalue_o   { std::function<void(double)> fn; void operator()(Opts& o) const { o.on_value = fn; } };

// 按钮形态
struct asbutton_o  { int v; void operator()(Opts& o) const { o.button_kind = v; } };
struct toggleopt_o { bool v; void operator()(Opts& o) const { o.button_kind = 1; o.toggled = v; } };
struct repeatopt_o { long v; void operator()(Opts& o) const { o.button_kind = 2; o.repeat_ms = v; } };
struct toggled_o   { bool v; void operator()(Opts& o) const { o.toggled = v; } };
struct repeatms_o  { long v; void operator()(Opts& o) const { o.repeat_ms = v; } };
struct onpickidx_o { std::function<void(int)> fn; void operator()(Opts& o) const { o.on_pick_index = fn; } };
struct context_o   { bool v; void operator()(Opts& o) const { o.context = v; } };
struct src_o       { std::wstring v; void operator()(Opts& o) const { o.src = v; } };
struct fit_o       { int v; void operator()(Opts& o) const { o.fit_mode = v; } };
struct rating_o    { int v; void operator()(Opts& o) const { o.rating = v; } };
struct ratingmax_o { int v; void operator()(Opts& o) const { o.rating_max = v; } };
struct severity_o  { int v; void operator()(Opts& o) const { o.severity = v; } };
struct closable_o  { bool v; void operator()(Opts& o) const { o.closable = v; } };
struct expanded_o  { bool v; void operator()(Opts& o) const { o.expanded = v; } };
struct arrows_o    { bool v; void operator()(Opts& o) const { o.arrows = v; } };
struct progress_o    { float v; void operator()(Opts& o) const { o.progress = v; } };
struct indeterminate_o { bool v; void operator()(Opts& o) const { o.indeterminate = v; } };
struct thickness_o   { float v; void operator()(Opts& o) const { o.thickness = v; } };
struct password_o    { bool v;  void operator()(Opts& o) const { o.password = v; } };
struct maxlength_o   { int v;   void operator()(Opts& o) const { o.max_length = v; } };
struct readonly_o    { bool v;  void operator()(Opts& o) const { o.readonly = v; } };
struct minwidth_o    { float v; void operator()(Opts& o) const { o.min_width = v; } };
struct scrimclose_o  { bool v;  void operator()(Opts& o) const { o.close_on_scrim = v; } };
struct draggable_o   { bool v;  void operator()(Opts& o) const { o.draggable = v; } };
struct oktext_o      { std::wstring v; void operator()(Opts& o) const { o.ok_text = v; } };
struct canceltext_o  { std::wstring v; void operator()(Opts& o) const { o.cancel_text = v; } };

// 日期选择器
struct dateformat_o  { std::wstring v; void operator()(Opts& o) const { o.date_format = v; o.color_format = v; } };
struct hour_o     { int v; void operator()(Opts& o) const { o.hour = v; } };
struct minute_o   { int v; void operator()(Opts& o) const { o.minute = v; } };
struct second_o   { int v; void operator()(Opts& o) const { o.second = v; } };
struct seconds_o  { bool v; void operator()(Opts& o) const { o.show_seconds = v; } };
struct timefmt_o  { std::wstring v; void operator()(Opts& o) const { o.time_format = v; } };
struct ontime_o   { std::function<void(int,int,int)> v; void operator()(Opts& o) const { o.on_pick_time = v; } };
struct weeknames_o   { std::vector<std::wstring> v; void operator()(Opts& o) const { o.week_names = v; } };
struct monthnames_o  { std::vector<std::wstring> v; void operator()(Opts& o) const { o.month_names = v; } };
struct titlepattern_o{ std::wstring v; void operator()(Opts& o) const { o.title_pattern = v; } };
struct cleartext_o   { std::wstring v; void operator()(Opts& o) const { o.clear_text = v; } };
struct todaytext_o   { std::wstring v; void operator()(Opts& o) const { o.today_text = v; } };
struct weekstart_o   { int v; void operator()(Opts& o) const { o.week_start = v; } };
struct datetuple_o   { int y, m, d; void operator()(Opts& o) const { o.year = y; o.month = m; o.day = d; } };
struct pickdate_o    { std::function<void(int, int, int)> v;
                       void operator()(Opts& o) const { o.on_pick_date = v; } };

// 取色器
struct color_o       { unsigned v; void operator()(Opts& o) const { o.color = (int)(v & 0xFFFFFFu); } };
struct swatches_o    { std::vector<unsigned> v; void operator()(Opts& o) const { o.swatches = v; } };
struct pickcolor_o   { std::function<void(int, int, int)> v;
                       void operator()(Opts& o) const { o.on_pick_color = v; } };

inline width_o    width(float v)      { return {v}; }
inline height_o   height(float v)     { return {v}; }
inline flex_o     flex(float v)       { return {v}; }
inline padding_o  padding(float v)    { return {v}; }
inline spacing_o  spacing(float v)    { return {v}; }
inline gap_o      gap(float v)        { return {v}; }
inline maxwidth_o max_width(float v)  { return {v}; }
inline radius_o   radius(float v)     { return {v}; }

inline title_o    title(const std::wstring& v)    { return {v}; }
inline title_o    title(const char* v)            { return {to_wide(v)}; }
inline title_o    title(const std::string& v)     { return {to_wide(v)}; }
inline subtitle_o subtitle(const std::wstring& v) { return {v}; }
inline subtitle_o subtitle(const char* v)         { return {to_wide(v)}; }
inline subtitle_o subtitle(const std::string& v)  { return {to_wide(v)}; }
inline text_o     text(const std::wstring& v)     { return {v}; }
inline text_o     text(const char* v)             { return {to_wide(v)}; }
inline text_o     text(const std::string& v)      { return {to_wide(v)}; }
inline icon_o     icon(const std::wstring& v)     { return {v}; }
inline icon_o     icon(const char* v)             { return {to_wide(v)}; }

inline theme_o    theme(Theme v)        { return {v}; }
inline variant_o  variant(Variant v)    { return {v}; }
inline variant_o  primary()             { return {Variant::Primary}; }
inline variant_o  standard()            { return {Variant::Standard}; }
inline variant_o  subtle()              { return {Variant::Subtle}; }
inline scale_o    scale(TextScale v)    { return {v}; }
inline role_o     role(Role v)          { return {v}; }
inline align_o    align(HAlign v)       { return {v}; }
inline accent_o   accent(uint32_t v)    { return {v}; }

inline click_o    click(const char* v)        { return {std::string(v)}; }
inline click_o    click(const std::string& v) { return {std::string(v)}; }
inline change_o   change(const char* v)        { return {std::string(v)}; }
inline change_o   change(const std::string& v) { return {std::string(v)}; }
inline checked_o  checked(bool v)     { return {v}; }
inline spinning_o spinning(bool v)    { return {v}; }
inline wrap_o     wrap(bool v)        { return {v}; }
inline stretch_o  stretch(bool v)     { return {v}; }
inline enabled_o  enabled(bool v)     { return {v}; }
inline shadow_o   shadow(bool v)      { return {v}; }
inline acrylic_o  acrylic(bool v = true) { return {v}; }

// 下拉框参数。items 支持 {"a","b"} / {L"a",L"b"} / std::vector<std::wstring>
inline items_o items(const std::vector<std::wstring>& v) { return {v}; }
inline items_o items(std::initializer_list<std::wstring> v) { return {std::vector<std::wstring>(v)}; }
inline items_o items(std::initializer_list<const wchar_t*> v) {
    std::vector<std::wstring> r; r.reserve(v.size());
    for (auto s : v) r.push_back(s ? std::wstring(s) : std::wstring());
    return {r};
}
inline items_o items(std::initializer_list<const char*> v) {
    std::vector<std::wstring> r; r.reserve(v.size());
    for (auto s : v) r.push_back(to_wide(s));
    return {r};
}
inline items_o items(std::initializer_list<std::string> v) {
    std::vector<std::wstring> r; r.reserve(v.size());
    for (auto& s : v) r.push_back(to_wide(s));
    return {r};
}
inline placeholder_o placeholder(const std::wstring& v) { return {v}; }
inline placeholder_o placeholder(const char* v)          { return {to_wide(v)}; }
inline selected_o  selected(int v)         { return {v}; }
inline overflow_o  allow_overflow(bool v)  { return {v}; }
inline menu_o      menu(bool v)           { return {v}; }
inline menubar_o   menubar(bool v)        { return {v}; }
inline hoveropen_o hover_open(bool v)     { return {v}; }
inline hoverdelay_o hover_delay(float v)  { return {v}; }
inline overflow_o  overflow(bool v)        { return {v}; }
inline open_o      open(bool v)            { return {v}; }
inline maxheight_o max_height(float v)     { return {v}; }
inline rowheight_o row_height(float v)     { return {v}; }
inline value_o     value(const std::wstring& v) { return {v}; }
inline value_o     value(const char* v)         { return {to_wide(v)}; }

// 数字框：值 / 上下限 / 步长 / 小数位 / 单位后缀 / 步进样式（0 上下箭头 / 1 左右加减 / 2 不画）
inline num_o       num(double v)                { return {v}; }
inline nummin_o    num_min(double v)            { return {v}; }
inline nummax_o    num_max(double v)            { return {v}; }
inline numstep_o   num_step(double v)           { return {v}; }
inline decimals_o  decimals(int v)              { return {v}; }
inline suffix_o    suffix(const std::wstring& v) { return {v}; }
inline suffix_o    suffix(const char* v)         { return {to_wide(v)}; }
inline numstyle_o  number_style(int v)          { return {v}; }
inline repeatdelay_o repeat_delay_ms(long v)    { return {v}; }
inline onvalue_o   on_value(std::function<void(double)> fn) { return {std::move(fn)}; }

// 按钮形态：切换 / 重复（按住连发）/ 超链接 / 拆分 / 下拉
inline toggleopt_o as_toggle(bool on = false)      { return {on}; }
inline repeatopt_o as_repeat(long delay_ms = 400)  { return {delay_ms}; }
inline asbutton_o  as_hyperlink()                  { return {3}; }
inline asbutton_o  as_split()                      { return {4}; }
inline asbutton_o  as_dropdown()                   { return {5}; }
inline toggled_o    toggled(bool on = true)        { return {on}; }
inline onpickidx_o  on_pick_index(std::function<void(int)> fn) { return {std::move(fn)}; }
inline context_o    context(bool on = true)        { return {on}; }

// 图片：src 路径，cover 裁满（默认）/ contain 完整装入
inline src_o        src(const std::wstring& v)     { return {v}; }
inline src_o        src(const char* v)             { return {to_wide(v)}; }
inline fit_o        cover()                        { return {0}; }
inline fit_o        contain()                      { return {1}; }

// 星级 / 提示条 / 折叠面板
inline rating_o     rating(int v)                   { return {v}; }
inline ratingmax_o  stars(int v)                    { return {v}; }
inline severity_o   severity(int v)                 { return {v}; }
inline closable_o   closable(bool on = true)        { return {on}; }
inline expanded_o   expanded(bool on = true)        { return {on}; }
inline arrows_o     arrows(bool on = true)          { return {on}; }

inline progress_o    progress(float v)        { return {v}; }
inline indeterminate_o indeterminate(bool v)  { return {v}; }
inline thickness_o   thickness(float v)       { return {v}; }

inline password_o    password(bool v)        { return {v}; }
inline maxlength_o   max_length(int v)       { return {v}; }
inline readonly_o    readonly(bool v)        { return {v}; }
inline minwidth_o    min_width(float v)      { return {v}; }
inline scrimclose_o  close_on_scrim(bool v)  { return {v}; }
inline draggable_o   draggable(bool v = true) { return {v}; }
inline oktext_o       ok_text(const std::wstring& v) { return {v}; }
inline oktext_o       ok_text(const char* v)         { return {to_wide(v)}; }
inline oktext_o       ok_text(const std::string& v)  { return {to_wide(v)}; }
inline canceltext_o   cancel_text(const std::wstring& v) { return {v}; }
inline canceltext_o   cancel_text(const char* v)         { return {to_wide(v)}; }
inline canceltext_o   cancel_text(const std::string& v)  { return {to_wide(v)}; }

// 日期选择器：format 是"程序指定的回填格式"，不给就是 4y-2m-2d
inline dateformat_o   format(const std::wstring& v) { return {v}; }
inline dateformat_o   format(const char* v)         { return {to_wide(v)}; }
inline dateformat_o   format(const std::string& v)  { return {to_wide(v)}; }

// 时间面板
inline hour_o         hour(int v)                    { return {v}; }
inline minute_o       minute(int v)                  { return {v}; }
inline second_o       second(int v)                  { return {v}; }
inline seconds_o      show_seconds(bool on = true)   { return {on}; }
inline timefmt_o      time_format(const std::wstring& v) { return {v}; }
inline timefmt_o      time_format(const char* v)         { return {to_wide(v)}; }
inline ontime_o       on_pick_time(std::function<void(int,int,int)> f) { return {std::move(f)}; }
inline weeknames_o    week_names(const std::vector<std::wstring>& v) { return {v}; }
inline weeknames_o    week_names(std::initializer_list<const wchar_t*> v) {
    std::vector<std::wstring> r; for (auto s : v) r.push_back(s ? std::wstring(s) : std::wstring());
    return {r};
}
inline weeknames_o    week_names(std::initializer_list<const char*> v) {
    std::vector<std::wstring> r; for (auto s : v) r.push_back(to_wide(s));
    return {r};
}
inline monthnames_o   month_names(std::initializer_list<const wchar_t*> v) {
    std::vector<std::wstring> r; for (auto s : v) r.push_back(s ? std::wstring(s) : std::wstring());
    return {r};
}
inline monthnames_o   month_names(std::initializer_list<const char*> v) {
    std::vector<std::wstring> r; for (auto s : v) r.push_back(to_wide(s));
    return {r};
}
inline titlepattern_o title_pattern(const std::wstring& v) { return {v}; }
inline titlepattern_o title_pattern(const char* v)         { return {to_wide(v)}; }
inline titlepattern_o title_pattern(const std::string& v)  { return {to_wide(v)}; }
inline cleartext_o    clear_text(const std::wstring& v) { return {v}; }
inline cleartext_o    clear_text(const char* v)         { return {to_wide(v)}; }
inline cleartext_o    clear_text(const std::string& v)  { return {to_wide(v)}; }
inline todaytext_o    today_text(const std::wstring& v) { return {v}; }
inline todaytext_o    today_text(const char* v)         { return {to_wide(v)}; }
inline todaytext_o    today_text(const std::string& v)  { return {to_wide(v)}; }
inline weekstart_o    week_start(int v)                 { return {v}; }
inline datetuple_o    date(int y, int m, int d)         { return {y, m, d}; }
inline pickdate_o     on_pick_date(std::function<void(int, int, int)> fn) { return {std::move(fn)}; }

inline color_o        color(unsigned v)                  { return {v & 0xFFFFFFu}; }
inline swatches_o      swatches(std::vector<unsigned> v)  { return {std::move(v)}; }
inline swatches_o      swatches(std::initializer_list<unsigned> v) { return {std::vector<unsigned>(v)}; }
inline pickcolor_o     on_pick_color(std::function<void(int, int, int)> fn) { return {std::move(fn)}; }

}  // namespace kw

// 把任意个选项应用到一张 Opts 上（C++17 折叠表达式）。
template <class... Args>
inline void apply_opts(Opts& o, Args&&... args) {
    (args(o), ...);
}

}  // namespace w11ui
