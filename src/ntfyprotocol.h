// SPDX-License-Identifier: GPL-2.0-or-later
//
// The part of the ntfy channel that is pure logic: turning a line of the
// server's JSON stream into an order, or into nothing.
//
// It lives on its own so it can be tested without a network, a server or an
// event loop -- and it deserves that, because it is where the channel's only
// real defence is applied. On a public ntfy server anybody who guesses the
// topic can publish to it; the key checked here is what stands between that
// stranger and a phone that starts screaming.

#pragma once

#include <QString>

namespace NtfyProtocol
{
// Returns the order with the key stripped, or an empty string if this line is
// not one: a keepalive, an open event, something published by somebody who does
// not know the key, or the key on its own with no verb after it.
//
// The key must be the FIRST WHOLE WORD. A message that merely mentions it does
// nothing -- otherwise any conversation quoting your key would fire orders.
// Compared case-insensitively, because whatever published the message may well
// have capitalised the line.
QString commandFrom(const QByteArray &jsonLine, const QString &key);
}
