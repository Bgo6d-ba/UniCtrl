/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LanProtocol.h"

#include <QElapsedTimer>
#include <QNetworkInterface>

namespace deskflow::gui::lan {

namespace {

// A flood of datagrams or connections asks on every one, and the interfaces are slow to list
constexpr qint64 kInterfacesMaxAgeMs = 1000;

struct Interfaces
{
  std::optional<QList<LocalNetwork>> forTest;
  QList<LocalNetwork> networks;
  QElapsedTimer age;
};

Interfaces &interfaces()
{
  static Interfaces s_interfaces;
  return s_interfaces;
}

QList<LocalNetwork> localNetworks()
{
  auto &known = interfaces();
  if (known.forTest) {
    return *known.forTest;
  }
  if (known.age.isValid() && !known.age.hasExpired(kInterfacesMaxAgeMs)) {
    return known.networks;
  }

  known.networks.clear();
  const auto all = QNetworkInterface::allInterfaces();
  for (const auto &networkInterface : all) {
    const auto flags = networkInterface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) || flags.testFlag(QNetworkInterface::IsLoopBack)) {
      continue;
    }
    const auto entries = networkInterface.addressEntries();
    for (const auto &entry : entries) {
      if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
        known.networks.append({entry.ip(), entry.prefixLength()});
      }
    }
  }
  known.age.restart();
  return known.networks;
}

bool isOnSubnet(quint32 peer, const LocalNetwork &network)
{
  bool isIPv4 = false;
  const auto address = network.address.toIPv4Address(&isIPv4);
  // a prefix of 0 would be every address, and one that isn't valid is -1
  if (!isIPv4 || network.prefixLength < 1 || network.prefixLength > 32) {
    return false;
  }
  const quint32 mask = network.prefixLength == 32 ? 0xFFFFFFFFU : ~(0xFFFFFFFFU >> network.prefixLength);
  return (peer & mask) == (address & mask);
}

} // namespace

bool isLocalPeer(const QHostAddress &address)
{
  if (address.isLoopback()) {
    return true;
  }
  bool isIPv4 = false;
  const auto peer = address.toIPv4Address(&isIPv4);
  if (!isIPv4) {
    return false;
  }
  const auto networks = localNetworks();
  return std::ranges::any_of(networks, [peer](const LocalNetwork &network) { return isOnSubnet(peer, network); });
}

void setLocalNetworksForTest(const std::optional<QList<LocalNetwork>> &networks)
{
  interfaces().forTest = networks;
}

} // namespace deskflow::gui::lan
