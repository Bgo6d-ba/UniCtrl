/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "arch/Arch.h"

#include <QTemporaryDir>
#include <QTest>

class ServerConfigTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void equalityCheck();
  void equalityCheck_diff_options();
  void equalityCheck_diff_alias();
  void equalityCheck_diff_filters();
  void equalityCheck_diff_address();
  void equalityCheck_diff_neighbours1();
  void equalityCheck_diff_neighbours2();
  void equalityCheck_diff_neighbours3();
  void restartServerAction_requestsQuit();
  void read_links();
  void read_linkToAlias_usesCanonicalName();
  void read_optionsBuildFilterRules();
  void copy_clonesFilterRules();
  void read_takesSettingsFromOptions();
  void read_ignoresMovedSectionsAndComments();
  void read_invalid_data();
  void read_invalid();
  void read_failureLeavesConfigUnchanged();
  void write_roundTrip();
  void write_connectCondition_roundTrip();
  void isValidScreenName();

private:
  Arch m_arch;
};
