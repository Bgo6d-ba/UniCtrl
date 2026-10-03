/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"
#include "deskflow/AppUtil.h"

#include <memory>

#include <QTemporaryDir>
#include <QTest>

class ServerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();

  void SwitchToScreenInfo_alloc_screen();
  void KeyboardBroadcastInfo_alloc_stateAndSceens();

  void edgeCrossing_switchesToLinkedScreen_data();
  void edgeCrossing_switchesToLinkedScreen();
  void edgeCrossing_noNeighbour_staysOnPrimary();
  void secondaryMotion_returnsToPrimaryOutsideJumpZone();
  void secondaryMotion_clampsAtEdgeWithoutNeighbour();
  void secondaryMotion_largeMoveCrossesSeveralScreens();
  void neighbour_skipsOfflineScreen();
  void neighbour_offlineLinkCycle_terminates();
  void lockCursor_eventBlocksAndReleasesSwitching();
  void lockCursor_scrollLockAtStartup();
  void lockCursor_disabledByOption();
  void lockCursor_mouseButtonHeld();
  void switchDelay_switchesOnTimer();
  void switchDelay_cancelledWhenLeavingEdge();
  void switchDelay_onSecondary();
  void switchDelay_onSecondaryBottomEdge_keepsWaitingAlongEdge();
  void twoTap_secondTapSwitches();
  void twoTap_withoutLeavingTapZone_doesNotSwitch();
  void corner_blocksSwitch();
  void clipboardGrab_notifiesOtherScreens();
  void clipboardGrab_ignoresStaleSequence();
  void clipboardChange_fromNonOwner_isIgnored();
  void clipboardGrab_ignoredWhenSharingDisabled();
  void clipboardChanged_propagatesToActiveScreen();
  void adoptClient_sendsOptionsAndNotifies();
  void adoptClient_duplicateName_closedBusy();
  void adoptClient_unknownName_closedAndDeletedOnDisconnect();
  void adoptClient_nonRemoteUnknownClient_notClosed();
  void clientDisconnect_whileActive_returnsToPrimary();
  void closeTimeout_deletesClientAndTimer();
  void teardown_removesAllHandlersAndTimers();
  void switchToScreenEvent_jumpsToScreen();
  void switchInDirectionEvent_jumpsToNeighbour();
  void toggleScreenEvent_cyclesScreens();
  void keyboardBroadcast_sendsKeysToAllScreens();
  void keyboardBroadcast_limitedToScreens();
  void keys_relayedToActiveScreen();
  void mouseEvents_relayedToActiveScreen();
  void screensaver_returnsToPreviousScreen();
  void relativeMoves_whenLocked();
  void statusSink_reportsConnectionsAndRejections();

private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_settingsDir;
  std::unique_ptr<AppUtil> m_appUtil;
};
