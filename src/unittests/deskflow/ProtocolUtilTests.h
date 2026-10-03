/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class ProtocolUtilTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void readf_string_roundTrip();
  void readf_string_maxLength_accepted();
  void readf_string_oversizedLength_throws();
  void readf_truncatedInput_returnsFalse();
  void readf_literalMismatch_returnsFalse();
  void roundTrip_everyMessage();
  void writef_hello_fixedLengthName();
  void writef_fixedLengthStringWrongLength_throws();
  void writef_checkedFormat();
  void packetStreamFilter_emptyPacket_nextPacketReadable();

private:
  Log m_log;
};
