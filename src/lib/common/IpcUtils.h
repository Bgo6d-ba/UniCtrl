/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QStringList>

namespace deskflow::ipc {

/**
 * @brief Splits an IPC message on the first '=' only, so args may contain '='.
 * @return The command, followed by the args if the message has any.
 */
QStringList splitMessage(const QString &message);

/**
 * @brief The core IPC server name shared by the core and the GUI.
 * On Unix this is a full path in a directory only the current user can access.
 * @return An empty string if no private directory could be found.
 */
QString coreServerName();

} // namespace deskflow::ipc
