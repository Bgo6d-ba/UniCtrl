/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <common/Settings.h>

struct Fingerprint;

namespace deskflow::gui::TlsUtility {

inline constexpr int kMinKeySize = 2048;

bool generateCertificate();

/**
 * @brief Raises Settings::Security::KeySize to kMinKeySize when it is smaller
 * @return true when the key size had to be raised
 */
bool enforceMinKeySize();

/**
 * @brief Makes sure the certificate at Settings::Security::Certificate exists, generating it when missing
 * @param replaceInvalid also regenerate an existing certificate that fails isCertValid()
 * @return the SHA256 fingerprint of the certificate, or an empty array on failure
 */
QByteArray ensureCertificate(bool replaceInvalid);

/**
 * @brief Checks whether a peer fingerprint is listed in a trusted fingerprint database
 * @param dbPath path of the database, see Settings::tlsTrustedServersDb and Settings::tlsTrustedClientsDb
 */
bool isTrusted(const QString &dbPath, const Fingerprint &fingerprint);

/**
 * @brief Adds a peer fingerprint to a trusted fingerprint database
 * @return false when the database could not be written
 */
bool addTrusted(const QString &dbPath, const Fingerprint &fingerprint);

/**
 * @brief Checks the settings values Settings::Security::TlsEnabled
 * @return true when tls is enabled
 */
bool isEnabled();

/**
 * @brief isCertValid
 * @param certPath the path of the file to check, when not set uses Settings::Security::Certificate value
 * @return true if the certificate is valid
 */
bool isCertValid(const QString &certPath = Settings::value(Settings::Security::Certificate).toString());

/**
 * @brief Get the lenght of a key
 * @param certPath path of the file to check, when unset will use the value of Settings::Security::Certificate
 * @return the bitsize of the key or -1 if there was an error reading the file
 */
int getCertKeyLength(const QString &certPath = Settings::value(Settings::Security::Certificate).toString());

/**
 * @brief get the SHA256 fingerprint of a certificatefile.
 * @param certPath path of the file to fingerprint, when not set uses the vaule of Settings::Security::Certificate
 * @return A QByteArray of the SHA256 fingerprint of the file or empty array if invalid file is passed
 */
// clang-format off
  QByteArray certFingerprint(const QString &certPath = Settings::value(Settings::Security::Certificate).toString());
// clang-format on
} // namespace deskflow::gui::TlsUtility
