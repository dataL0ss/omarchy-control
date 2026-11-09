#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

#include "env.h"

// What decides blur on Omarchy, read without running anything: which of Omarchy's
// toggles are on, and the values its glass sets when one of them is. Omarchy's
// defaults turn blur off; Glassy windows and the Glassy bar turn it on through
// default/hypr/glass.lua; Animations and blur off turns it off again, though the
// toggles load in name order, so Glassy windows, loading last, keep theirs.
namespace Blur {

struct Toggles {
    bool windows = false;       // window-glassy.lua: every see-through window and shell surface.
    bool bar = false;           // bar-glassy.lua: the bar only.
    bool noAnimations = false;  // no-animations.lua: no animations, blur, or shadows.
};

Toggles toggles(const Env &env);

// Omarchy's glass, as `key = value` pairs from glass.lua. Empty when the file is
// missing or Omarchy has moved it, and then only Hyprland's live values are shown.
QHash<QString, QString> glassValues(const Env &env);
QHash<QString, QString> parseGlass(const QByteArray &lua);

// Where blur shows, from the toggles and Hyprland's live switch: "windows" (behind
// Glassy windows and the shell's surfaces), "bar", "on" (turned on by the user's own
// config), or "off".
QString status(const Toggles &toggles, bool enabled);
// A sentence for the status row, or nothing when the status says it all.
QString statusReason(const Toggles &toggles, bool enabled);

// The named strength whose size and passes these are, from the entry's presets, or
// "size×passes" when they match none. A preset with no values stands for Omarchy's
// glass, compared against `glass`.
QString strengthName(const QJsonObject &presets, const QString &size, const QString &passes,
                     const QHash<QString, QString> &glass);

}  // namespace Blur
