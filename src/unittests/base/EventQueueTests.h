/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QObject>

class EventQueueTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void dispatchEvent_noHandler_returnsFalse();
  void dispatchEvent_noTypeHandler_dispatchesUnknownHandler();
  void dispatchEvent_handlerRemovesItself_keepsHandlerAliveUntilReturn();
  void addEvent_dataObjectWithFlags_deletedOnceAfterDispatch();
  void deleteData_dontFreeData_keepsDataObject();
  void loop_addEventFromOtherThreadsWhileStarting_deliversAll();
  void waitForReady_afterLoopStarted_returns();
  void newOneShotTimer_fromOtherThread_fires();

private:
  Arch m_arch;
  Log m_log;
};
