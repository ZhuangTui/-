// w11ui ----------------------------------------------------------------
// Windows 11 风格 C++ UI 库（头文件版）
//
//   #include <w11ui/w11ui.hpp>
//
//   int main() {
//       auto app = w11ui::Window(w11ui::kw::title("Demo"),
//                                w11ui::kw::width(960),
//                                w11ui::kw::height(640));
//       app.on("a", [](w11ui::Event& e) { /* 点击事件的 "a" */ });
//       app.add(w11ui::ui::button(w11ui::kw::text("确定"),
//                                 w11ui::kw::click("a")));
//       return app.run();
//   }
#pragma once

#include "core.hpp"
#include "theme.hpp"
#include "painter.hpp"
#include "kwargs.hpp"
#include "widgets.hpp"
#include "mnicompt.hpp"
#include "window.hpp"
#include "snapshot.hpp"
#include "error.hpp"
