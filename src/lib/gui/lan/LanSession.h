/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "LanBeacon.h"
#include "Pairing.h"

#include "common/Enums.h"
#include "common/Settings.h"
#include "gui/widgets/HostJoinWidget.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

class QTimer;

namespace deskflow::gui::lan {

//! How long a client that a join started may take to connect before the join is given up
inline constexpr int kJoinWatchdogMs = 20000;
//! The most computers that pair with one hosting, whoever knows the code: trust stays until the user removes it
inline constexpr int kMaxPairingsPerSession = 16;

/**
 * @brief The Host / Join controller: discovery, pairing and the settings that follow from them
 *
 * It holds no widgets. HostJoinWidget gets its state through the signals below and its requests arrive at the slots;
 * MainWindow runs the core when asked to and reports what the core does.
 *
 * Hosting forces TLS and peer checks, announces this computer and pairs the computers that enter the code (or
 * password): their fingerprint is trusted and their screen is requested for the layout. The code stays valid until
 * hosting stops, a new one is asked for, or too many wrong ones are entered; after kMaxPairingsPerSession computers
 * have paired, nobody can until there is a new one. Joining pairs with the host, trusts its fingerprint, points the
 * client at it and asks for the core to start; a client that is not connected after kJoinWatchdogMs is given up.
 * Nearby hosts are listed while the view asks for it, and never while this computer is hosting.
 *
 * The session follows the core: hosting or joining ends when the core stops or leaves its mode. Nothing starts
 * without the user: in the simple view a server that runs is taken as hosting only when the user hosted before
 * (Lan::HostingEnabled) and it already has the security that pairing relies on, which is not changed, and one that
 * is not is left alone (legacySessionChanged()). A client that runs shows as joined, for display only.
 */
class LanSession : public QObject
{
  Q_OBJECT

public:
  explicit LanSession(QObject *parent = nullptr);

  bool isHosting() const
  {
    return m_hosting;
  }

  /**
   * @brief The core met a peer whose certificate is not trusted
   * @return true while this computer hosts, or joins a host on the user's request: pairing is what makes a peer
   * trusted, so the user is not asked. Hosting ignores the peer, a join fails and has to be paired again.
   */
  bool rejectUntrustedPeer();

  //! The ports and addresses of the network; only tests change them
  struct Network
  {
    quint16 browsePort = kBeaconPort;    //!< UDP, where announcements are heard
    quint16 announcePort = kBeaconPort;  //!< UDP, where announcements are sent
    quint16 pairingPort = kPairingPort;  //!< TCP, where pairing listens
    QList<QHostAddress> announceTargets; //!< where announcements are sent, the broadcast addresses when empty
  };

  //! Test hook, before browsing or hosting. Ports 0 pick any.
  void setNetworkForTest(const Network &network);
  //! Test hook: the UDP port browsing listens on, for tests that let it pick
  quint16 browsePort() const;
  //! Test hook: replaces the time a stopped core may take to start again before hosting or joining ends
  void setCoreStopGraceForTest(int ms);
  //! Test hook: replaces kJoinWatchdogMs
  void setJoinWatchdogForTest(int ms);
  //! Test hook: the pairing server, to make it report what a network would
  PairingServer *pairingServerForTest() const
  {
    return m_server;
  }

public Q_SLOTS:
  // What the user asks for in the Host / Join view

  //! Announces this computer and shows a code, then asks for the server to start
  void startHosting();
  //! Ends hosting and asks for the core to stop
  void stopHosting();
  //! Replaces the code or password with a new random code
  void newCode();
  //! Computers join with this password, at least HostJoinWidget::MinCustomPasswordLength characters, instead of a code
  void setCustomPassword(const QString &password);
  //! Lists the hosts on the network for as long as this computer is not hosting
  void startBrowsing();
  void stopBrowsing();
  //! Pairs with a nearby host on its code or password, then asks for the client to start
  void join(const QString &hostId, const QString &code);
  //! Joins a host paired with before, without a code
  void rejoin(const QString &hostId);
  //! Ends joining and asks for the client to stop, or cancels the pairing in progress
  void leave();

  // What MainWindow tells about the application

  //! In the simple view a running core is taken up (see above), in the advanced view browsing stops
  void setSimpleView(bool simple);
  //! Applies changed names, port, certificate and security settings to the hosting in progress
  void refresh();
  void coreProcessStateChanged(deskflow::core::ProcessState state);
  void coreConnectionStateChanged(deskflow::core::ConnectionState state);
  //! The screen names of the computers connected to this server
  void connectedClientsChanged(const QStringList &screenNames);

Q_SIGNALS:
  // For HostJoinWidget

  void hostsChanged(const QList<HostListEntry> &hosts);
  void hostingChanged(bool hosting);
  //! Empty while a custom password is used
  void codeChanged(const QString &code);
  //! The display names of the computers connected to this host
  void joinedComputersChanged(const QStringList &names);
  void joinStateChanged(HostJoinWidget::JoinState state, const QString &hostName);
  //! A computer entered the right code and can now connect
  void clientJoined(const QString &displayName);
  //! Something the user should know: a failure, or a notice such as a wrong code entered on the host. Empty when a
  //! notice does not hold any more.
  void error(const QString &text);
  //! In the simple view, a server runs that was not set up by Host: it is left as it is
  void legacySessionChanged(bool legacy);

  // For MainWindow

  //! Run the core in this mode, restarting it when it runs
  void startCoreRequested(Settings::CoreMode mode);
  void stopCoreRequested();
  //! Put the screen of this computer in the layout, restarting the core when that changes it
  void addClientScreenRequested(const deskflow::gui::lan::PeerIdentity &client);

private:
  bool beginHosting(bool adopt);
  void endHosting();
  std::optional<PeerIdentity> ownIdentity(quint16 corePort);
  void flushNotice();
  void requestCoreStop();
  void pairedWithClient(const PeerIdentity &client, const QHostAddress &from);
  void pairingLockedOut(int seconds);
  void usePassword(const QString &password, bool custom);

  void updateBrowser();
  void publishHosts();
  std::optional<DiscoveredHost> nearbyHost(const QString &hostId) const;

  void pairedWithHost(const PeerIdentity &host);
  void pairingFailed(PairingError reason, const QString &detail);
  void startClient();
  void adoptClient();
  void cancelJoin();
  void failJoin(const QString &text, bool forgetHost);
  void joinTimedOut();
  void setJoinState(HostJoinWidget::JoinState state);

  void reconcile();
  void setLegacy(bool legacy);
  void coreStopped();
  QStringList displayNames(const QStringList &screenNames) const;

  LanAnnouncer *m_announcer;
  LanBrowser *m_browser;
  PairingServer *m_server;
  PairingClient *m_client;
  QTimer *m_coreStopTimer;
  QTimer *m_joinWatchdog;

  Network m_network;

  bool m_simple = false;
  bool m_browsing = false;
  bool m_browserRunning = false;
  bool m_legacy = false;

  bool m_hosting = false;
  bool m_customPassword = false;
  QString m_password;
  QString m_fingerprint;
  QStringList m_connectedClients;
  int m_pairedCount = 0;        //!< computers that paired since the code or password was set
  bool m_capped = false;        //!< kMaxPairingsPerSession was reached, and the user was told
  QString m_notice;             //!< for when the page it belongs on shows
  bool m_stopRequested = false; //!< the core was asked to stop and has not yet

  HostJoinWidget::JoinState m_joinState = HostJoinWidget::JoinState::Idle;
  bool m_pairing = false; //!< joining, and the pairing with m_target is not done: the client is not asked for yet
  bool m_adopted = false; //!< the client that is shown as joined was not started by a join
  bool m_rejoin = false;  //!< the join in progress is one without a code
  DiscoveredHost m_target;

  deskflow::core::ProcessState m_coreState = deskflow::core::ProcessState::Stopped;
  deskflow::core::ConnectionState m_connection = deskflow::core::ConnectionState::Disconnected;
};

} // namespace deskflow::gui::lan
