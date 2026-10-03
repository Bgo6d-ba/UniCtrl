/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StatusSink.h"

#include <QMetaEnum>

#include <atomic>

namespace deskflow {

namespace {
std::atomic<IStatusSink *> s_sink = nullptr;
} // namespace

void setStatusSink(IStatusSink *sink)
{
  s_sink = sink;
}

IStatusSink *statusSink()
{
  return s_sink;
}

void reportStatus(const QString &command, const QString &args)
{
  if (auto *sink = s_sink.load(); sink != nullptr) {
    sink->sendStatus(command, args);
  }
}

void reportConnectionState(core::ConnectionState state)
{
  const auto metaEnum = QMetaEnum::fromType<core::ConnectionState>();
  reportStatus(QStringLiteral("connectionState"), metaEnum.valueToKey(static_cast<int>(state)));
}

} // namespace deskflow
