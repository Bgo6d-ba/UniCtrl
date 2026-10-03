/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <QString>

namespace deskflow {

/**
 * Receives status updates (connection state, peer fingerprint, ...) from the lower layers.
 * The application installs one, e.g. the core IPC server forwarding them to the GUI.
 * Implementations may be called from any thread.
 */
class IStatusSink
{
public:
  virtual ~IStatusSink() = default;
  virtual void sendStatus(const QString &command, const QString &args) = 0;
};

/// Installs the process-wide sink; pass nullptr to remove it. The sink must outlive its installation.
void setStatusSink(IStatusSink *sink);
IStatusSink *statusSink();

/// Forwards to the installed sink, if any.
void reportStatus(const QString &command, const QString &args = {});
void reportConnectionState(core::ConnectionState state);

} // namespace deskflow
