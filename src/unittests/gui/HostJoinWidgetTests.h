/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class HostJoinWidget;
class QLabel;
class QPushButton;
class QTreeWidget;

class HostJoinWidgetTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void init();
  void cleanup();
  void startsAtHome();
  void hostingFollowsState();
  void hostPageButtons();
  void customPassword();
  void customPasswordIsChecked();
  void joinWithCode();
  void rejoinPairedHost();
  void keepsSelectedHost();
  void joinStates();
  void errorsClearOnPageChange();
  void editDisplayName();
  void browsingFollowsThePage();
  void browsingStopsWhenHidden();
  void legacyNotice();

private:
  QString page() const;
  QPushButton *button(const QString &name) const;
  QLabel *label(const QString &name) const;
  QTreeWidget *hostList() const;
  void browse(bool withHosts = true);

  HostJoinWidget *m_widget = nullptr;
};
