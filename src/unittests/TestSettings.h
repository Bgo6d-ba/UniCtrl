/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Settings.h"

#include <QTemporaryDir>

// Gives a test suite private settings and state files, so suites can run in
// parallel and never touch the user's real ones.
inline void useTestSettings(const QTemporaryDir &dir)
{
  // Settings reads these when first constructed, before the files below are set
  qputenv("XDG_CONFIG_HOME", dir.path().toLocal8Bit());
  qputenv("XDG_STATE_HOME", dir.path().toLocal8Bit());
  Settings::setSettingsFile(dir.filePath(QStringLiteral("Deskflow.conf")));
  Settings::setStateFile(dir.filePath(QStringLiteral("Deskflow.state")));
}
