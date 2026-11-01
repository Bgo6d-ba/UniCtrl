/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class OSXDockSwipeTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void finishedSwipe_mapsToControlArrow_data();
  void finishedSwipe_mapsToControlArrow();
  void ignoresOtherEvents_data();
  void ignoresOtherEvents();
};
