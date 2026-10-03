/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QChar>
#include <QHostAddress>
#include <QList>
#include <QRandomGenerator>
#include <QString>

#include <algorithm>
#include <optional>

namespace deskflow::gui::lan {

inline constexpr quint16 kBeaconPort = 24802;  //!< UDP, host announcements
inline constexpr quint16 kPairingPort = 24803; //!< TCP, SPAKE2 pairing

inline constexpr int kBeaconIntervalMs = 2000;
inline constexpr int kHostExpiryMs = 7000;
inline constexpr int kMaxBeaconSize = 512;
inline constexpr int kMaxHosts = 64;
inline constexpr int kMaxHostsPerAddress = 4;       //!< distinct host ids one address may announce
inline constexpr int kHostsChangedIntervalMs = 250; //!< LanBrowser::hostsChanged comes this often at most

inline constexpr int kMaxFrameSize = 4096;
inline constexpr int kPairingDeadlineMs = 15000;
inline constexpr int kPairingHelloDeadlineMs = 3000; //!< the time a connection has to deliver its hello frame
inline constexpr int kMaxPairingSockets = 4;
inline constexpr int kMaxPairingSocketsPerAddress = 2;
inline constexpr int kMaxFailedAttemptsPerAddress = 5; //!< wrong codes from one address, then it is locked out
inline constexpr int kMaxFailedAttempts = 15;          //!< wrong codes from all addresses, then everyone is locked out
inline constexpr int kFailureWindowMs = 60000;
inline constexpr int kLockoutMs = 60000;

inline constexpr int kMaxDisplayNameLength = 64;
inline constexpr int kFingerprintLength = 64;

//! A new pairing code: 6 random digits
inline QString randomCode()
{
  return QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000000), 6, 10, QLatin1Char('0'));
}

//! 64 lowercase hex digits: the SHA-256 of a TLS certificate
inline bool isValidFingerprint(const QString &fingerprint)
{
  return fingerprint.size() == kFingerprintLength &&
         std::ranges::all_of(fingerprint, [](QChar c) { return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'); });
}

namespace detail {

// Characters that render as nothing, or nearly, although they are letters or symbols to Unicode
inline bool rendersBlank(char32_t c)
{
  return c == 0x115F || c == 0x1160 || c == 0x2800 || c == 0x3164 || c == 0xFFA0;
}

inline bool isVisible(char32_t c)
{
  return (QChar::isLetterOrNumber(c) || QChar::isPunct(c) || QChar::isSymbol(c)) && !rendersBlank(c);
}

// Controls and line breaks, and format characters (bidi overrides, zero width spaces, BOM, ...) that make a name read
// as another, except the joiners that emoji, Persian and Indic text are written with (ZWJ, ZWNJ) and the Mongolian
// vowel separator
inline bool isForbidden(char32_t c)
{
  switch (QChar::category(c)) {
  case QChar::Other_Control:
  case QChar::Separator_Line:
  case QChar::Separator_Paragraph:
    return true;
  case QChar::Other_Format:
    return c != 0x200D && c != 0x200C && c != 0x180E;
  default:
    return false;
  }
}

} // namespace detail

//! 1 to 64 UTF-16 units with at least one visible character, no control characters, line breaks or Unicode format
//! characters (except ZWJ, ZWNJ and the Mongolian vowel separator)
inline bool isValidDisplayName(const QString &name)
{
  if (name.size() > kMaxDisplayNameLength || !name.isValidUtf16()) {
    return false;
  }
  const auto characters = name.toUcs4();
  return std::ranges::none_of(characters, detail::isForbidden) && std::ranges::any_of(characters, detail::isVisible);
}

//! An IPv4 network of this computer: one of its addresses and the length of the subnet prefix
struct LocalNetwork
{
  QHostAddress address;
  int prefixLength = 0;
};

/**
 * @brief Whether a computer may announce hosts to this one or pair with it
 *
 * Only loopback and IPv4 addresses on the same subnet as an up, non-loopback interface of this computer may:
 * peers that are routed or on a VPN can't. The interfaces are looked up at most once a second; call it from the GUI
 * thread.
 */
bool isLocalPeer(const QHostAddress &address);

//! Test hook: these networks in place of the interfaces of this computer, nullopt for the interfaces again
void setLocalNetworksForTest(const std::optional<QList<LocalNetwork>> &networks);

} // namespace deskflow::gui::lan
