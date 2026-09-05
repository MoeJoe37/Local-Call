#pragma once

#include <QIcon>
#include <QRgb>
#include <QString>

class QAbstractButton;

/// Shared look-and-feel helpers.
///
/// The stylesheet used to be a 170-line raw string literal inside
/// MainWindow.cpp, with ~75 further inline setStyleSheet calls scattered around
/// it. Everything static now lives in :/theme/localcall.qss and is applied once
/// to the application, so a widget's appearance is described in exactly one
/// place.
namespace UiTheme {

/// Applies :/theme/localcall.qss application-wide. Returns false if the
/// resource is missing, in which case the app still runs, just unstyled.
bool apply();

QIcon icon(const QString& name);

/// Sets a resource icon and a square icon size in one call.
void applyIcon(QAbstractButton* button, const QString& iconName, int px = 18);

/// Assigns the qss "class" property and re-polishes, which Qt needs in order
/// to re-evaluate selectors on an already-shown widget.
void setClass(QWidget* widget, const QString& className);

/// The Catppuccin Mocha tokens that C++ genuinely needs — avatar tints, the
/// pixmap corner clip, painted chrome. Everything else belongs in
/// :/theme/localcall.qss; no new raw hex should appear in a .cpp file.
namespace Color {

constexpr QRgb Crust    = 0xFF11111B;
constexpr QRgb Mantle   = 0xFF181825;
constexpr QRgb Base     = 0xFF1E1E2E;
constexpr QRgb Surface0 = 0xFF313244;
constexpr QRgb Surface1 = 0xFF45475A;
constexpr QRgb Surface2 = 0xFF585B70;
constexpr QRgb Overlay0 = 0xFF6C7086;
constexpr QRgb Subtext0 = 0xFFA6ADC8;
constexpr QRgb Text     = 0xFFCDD6F4;

constexpr QRgb Mauve    = 0xFFCBA6F7;
constexpr QRgb Blue     = 0xFF89B4FA;
constexpr QRgb Green    = 0xFFA6E3A1;
constexpr QRgb Red      = 0xFFF38BA8;
constexpr QRgb Peach    = 0xFFFAB387;

/// Mauve composited at 28% over Crust — the outgoing bubble fill. Derived
/// rather than picked, so it stays in the family if the accent ever changes.
constexpr QRgb MauveDim = 0xFF3B2F55;

}  // namespace Color

}  // namespace UiTheme
