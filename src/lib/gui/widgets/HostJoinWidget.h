/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QWidget>

class QHideEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QStackedWidget;
class QTreeWidget;

struct HostListEntry
{
  QString id;
  QString name;
  bool paired = false; // joined before: shown as "Paired", Join reconnects without asking for a code
  bool locked = false; // joining needs a code: shown with a lock
};

/**
 * @brief The simple Host / Join view. It only shows what its slots are given and reports the user's choices through
 * its signals; it changes no settings and starts nothing.
 *
 * Home offers Host and Join. Host emits hostRequested(), and the Host page shows while setHostingState(true).
 * Join opens the nearby hosts, where Join emits rejoinRequested() for a paired host and otherwise asks for the code
 * and emits joinRequested(). setJoinState() then shows the progress or the connection; Idle returns to the nearby
 * hosts. Hosting wins over joining. showError() shows a message until the page changes.
 *
 * browsingChanged() tells when the nearby hosts are on screen, which is the only time they are looked for: not on
 * the other pages, not while the widget is hidden (Advanced shown, window hidden).
 *
 * Codes and passwords are passed on normalized: a six-digit code without spaces ("482 915" -> "482915"), anything
 * else trimmed. A password of your own is typed twice and has to be MinCustomPasswordLength characters at least.
 */
class HostJoinWidget : public QWidget
{
  Q_OBJECT

public:
  enum class JoinState
  {
    Idle,
    Joining,
    Connected
  };

  //! The shortest password a host may use instead of a code
  static constexpr int MinCustomPasswordLength = 8;

  explicit HostJoinWidget(QWidget *parent = nullptr);

public Q_SLOTS:
  void setDisplayName(const QString &name);
  void setHostingState(bool hosting);
  // Six digits, shown as "482 915"; empty while a custom password is used, which is never shown
  void setCode(const QString &code);
  void setJoinedComputers(const QStringList &names);
  // The selected host stays selected while it is listed
  void setNearbyHosts(const QList<HostListEntry> &hosts);
  void setJoinState(HostJoinWidget::JoinState state, const QString &hostName = {});
  // Empty text hides the message
  void showError(const QString &text);
  // The home page notes that a session set up by hand runs, and offers the Advanced view (advancedViewRequested)
  void setLegacySessionNotice(bool visible);

Q_SIGNALS:
  void hostRequested();
  void stopHostingRequested();
  void newCodeRequested();
  // At least MinCustomPasswordLength characters, confirmed by the user
  void customPasswordRequested(const QString &password);
  void arrangeScreensRequested();
  void joinRequested(const QString &hostId, const QString &code);
  void rejoinRequested(const QString &hostId);
  // Leave the host, or stop joining
  void leaveRequested();
  // Simplified, never empty
  void displayNameChanged(const QString &name);
  void advancedViewRequested();
  // True while the nearby hosts are on screen, false when they are not or the widget is hidden
  void browsingChanged(bool browsing);

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  QWidget *makeHomePage();
  QWidget *makeHostPage();
  QWidget *makeBrowsePage();
  QWidget *makeStatusPage();
  void updateText();
  void updatePage();
  void updateBrowsing();
  void updateJoinButton();
  void editName();
  void finishNameEdit(bool accept);
  void askPassword();
  void joinSelected();

  QString m_displayName;
  QString m_code;
  QString m_joinHostName;
  QStringList m_joined;
  QList<HostListEntry> m_hosts;
  bool m_hosting = false;
  bool m_browsing = false;      // the nearby hosts are the page to show
  bool m_browseVisible = false; // ... and they are on screen: what browsingChanged() last said
  JoinState m_joinState = JoinState::Idle;

  QLabel *m_lblYourName = nullptr;
  QLabel *m_lblName = nullptr;
  QLineEdit *m_editName = nullptr;
  QPushButton *m_btnEditName = nullptr;
  QPushButton *m_btnAdvanced = nullptr;
  QWidget *m_errorBox = nullptr;
  QLabel *m_lblError = nullptr;
  QStackedWidget *m_pages = nullptr;

  QWidget *m_homePage = nullptr;
  QPushButton *m_btnHost = nullptr;
  QLabel *m_lblHostChoice = nullptr;
  QPushButton *m_btnJoin = nullptr;
  QLabel *m_lblJoinChoice = nullptr;
  QWidget *m_legacyNotice = nullptr;
  QLabel *m_lblLegacy = nullptr;
  QPushButton *m_btnLegacyAdvanced = nullptr;

  QWidget *m_hostPage = nullptr;
  QLabel *m_lblHosting = nullptr;
  QLabel *m_lblCodeCaption = nullptr;
  QLabel *m_lblCode = nullptr;
  QPushButton *m_btnChangePassword = nullptr;
  QPushButton *m_btnNewCode = nullptr;
  QLabel *m_lblJoined = nullptr;
  QListWidget *m_listJoined = nullptr;
  QPushButton *m_btnArrange = nullptr;
  QPushButton *m_btnStopHosting = nullptr;

  QWidget *m_browsePage = nullptr;
  QLabel *m_lblNearby = nullptr;
  QLabel *m_lblNearbyHint = nullptr;
  QTreeWidget *m_listHosts = nullptr;
  QPushButton *m_btnBack = nullptr;
  QPushButton *m_btnJoinHost = nullptr;

  QWidget *m_statusPage = nullptr;
  QLabel *m_lblStatus = nullptr;
  QLabel *m_lblStatusHint = nullptr;
  QProgressBar *m_progress = nullptr;
  QPushButton *m_btnLeave = nullptr;
};
