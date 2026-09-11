// SPDX-License-Identifier: GPL-2.0-or-later
//
// Lost mode: a state of the phone, not a flag in a running process.
//
// The first thing somebody who picks up a stranger's phone does is restart it.
// So "locked because it was reported lost" has to be written down and read back
// at every start, or the lock is one power cycle deep and worth nothing.
//
// WHERE IT LIVES, AND THE HONEST LIMIT
// ------------------------------------
// In the user's own state directory, not under /var/lib as root. The plan said
// root; building it made the case weaker than it looked:
//
//   - the phone has one user, and the session starts at boot, so a file in
//     $XDG_STATE_HOME survives exactly the reboot that matters;
//   - between power-on and the session starting, the phone is at the lock
//     screen anyway -- there is no window to protect;
//   - smart-unlockd, which has to read this, is also a user service, so a
//     root-owned file would need a second channel for no gain;
//   - and root would only stop somebody who can already reach a shell, which
//     means they already have the PIN, which means lost mode is already over.
//
// What this does NOT survive, said plainly: a reflash, and a thief who knows
// your PIN. It is built for "perdido + robo casual", which is the threat that
// was chosen, and it does not pretend to be more.
//
// THE CONTRACT WITH SMART-UNLOCK
// ------------------------------
// smart-unlockd reads this same file before it disarms the lock. That is the
// whole reason the format is two boring lines and the path is fixed: it is an
// interface between two packages, so it has to be readable by anything and
// change never.

#pragma once

#include <QDateTime>
#include <QString>

struct LostMode {
    bool active = false;
    QString message;  // shown on the lock screen, e.g. "call 6xx xxx xxx"
    QDateTime since;
};

namespace LostModeState
{
// $XDG_STATE_HOME/lost-phone/, which is ~/.local/state/lost-phone/ by default.
QString dirPath();

// The file lost mode is written to: dirPath() + "lost-mode". Fixed on purpose:
// smart-unlock reads it by path, and it is a separate package, so the name is a
// contract between the two.
QString filePath();

// The file lost mode is read from: the one above, or "modo-perdido" -- its name
// before the rename -- when only that one exists.
QString readPath();

LostMode load();
void save(const LostMode &state);

// The only question smart-unlock needs to ask, cheap enough to call on every
// decision: is this phone in lost mode right now?
bool isActive();
}
