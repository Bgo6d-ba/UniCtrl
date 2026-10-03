/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class ClientProxyTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void keyDown_data();
  void keyDown();
  void keyRepeat_data();
  void keyRepeat();
  void keyUp_data();
  void keyUp();
  void isValidName_data();
  void isValidName();
  void handshakeInfo_readiesClient();
  void handshakeInfo_retriesAfterInvalidShape();
  void info_afterHandshake_changesShape();
  void grabClipboard_postsGrab();
  void unknownMessage_isSkipped();
  void truncatedCode_disconnects();
  void oversizedString_disconnects();
  void clipboardTransfer_postsChange_data();
  void clipboardTransfer_postsChange();
  void clipboardTransfer_overLimit_isDropped();
  void clientClipboard_reachesServerDesktop();

private:
  Log m_log;
};
