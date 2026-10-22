#pragma once
// Station colours and the app-wide dark style. Status colours mean the same everywhere:
// Ok = confirmed by received data, Warn = partial or waiting, Bad = failed or lost, Dim = no data.

class QApplication;
class QFont;

namespace Theme {
constexpr const char *Bg = "#0a101b";
constexpr const char *Panel = "#101927";
constexpr const char *Raised = "#162133";
constexpr const char *Border = "#213049";
constexpr const char *Text = "#d3deec";
constexpr const char *Dim = "#6b7c96";
constexpr const char *Accent = "#22c3e6";
constexpr const char *Ok = "#2fbf71";
constexpr const char *Warn = "#f0a020";
constexpr const char *Bad = "#ef4b4b";
}

void applyTheme(QApplication &app);  // palette, fonts and stylesheet; call once after constructing the app
QFont monoFont(int pointSize);       // numeric readouts
