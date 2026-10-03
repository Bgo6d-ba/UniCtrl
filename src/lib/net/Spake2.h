/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QString>

#include <array>

class Spake2Tests; // the unit test, the only one that may choose the scalars

namespace deskflow {

/**
 * @brief SPAKE2 (RFC 9382) with the P256-SHA256-HKDF-HMAC ciphersuite
 *
 * One instance runs one exchange: send share(), pass the peer's share to finish(), then swap
 * confirmation() and check the peer's with verifyPeer(). The exchange succeeded only once
 * verifyPeer() returns true.
 */
class Spake2
{
public:
  static constexpr int kScalarSize = 32;
  static constexpr int kShareSize = 65;
  static constexpr int kKeySize = 16;
  static constexpr int kMacSize = 32;

  enum class Role
  {
    A, //!< the client, sends its share first and blinds it with M
    B  //!< the host, blinds its share with N
  };

  /**
   * @brief The password scalar w: PBKDF2-HMAC-SHA256 of the NFC UTF-8 password, 64 bytes, mod the group order
   * @return 32 bytes big-endian, or empty on failure
   */
  static QByteArray passwordScalar(const QString &password);

  /**
   * @param w the password scalar, see passwordScalar(); an instance with an invalid one never succeeds
   */
  Spake2(Role role, const QByteArray &w);
  ~Spake2();
  Spake2(const Spake2 &) = delete;
  Spake2 &operator=(const Spake2 &) = delete;

  /**
   * @brief This side's share, pA = x*P + w*M or pB = y*P + w*N, as an uncompressed SEC1 point
   * @return empty on failure
   */
  QByteArray share();

  /**
   * @brief Derives the keys from the transcript; can be called once per instance
   * @param idA the identity of A (the client), idB that of B (the host)
   * @return false when the peer's share is not a valid point or the derivation failed
   */
  bool finish(const QByteArray &peerShare, const QByteArray &idA, const QByteArray &idB);

  //! This side's key confirmation MAC over the transcript, empty unless finish() succeeded
  QByteArray confirmation() const;

  //! Compares the peer's key confirmation MAC in constant time
  bool verifyPeer(const QByteArray &mac) const;

  //! The shared secret Ke, empty unless finish() succeeded; use it only once verifyPeer() succeeded
  QByteArray sharedKey() const;

private:
  friend class ::Spake2Tests;

  //! Test hook: use this 32 byte big-endian scalar (x or y) instead of a random one; call before share()
  void setScalarForTest(const QByteArray &scalar);

  struct Secrets
  {
    std::array<unsigned char, kScalarSize> w{};
    std::array<unsigned char, kScalarSize> scalar{};
    std::array<unsigned char, kKeySize> key{};
    std::array<unsigned char, kMacSize> mac{};
    std::array<unsigned char, kMacSize> peerMac{};
  };

  Role m_role;
  bool m_hasW = false;
  bool m_hasScalar = false;
  bool m_used = false;
  bool m_finished = false;
  QByteArray m_share;
  Secrets m_secrets;
};

} // namespace deskflow
