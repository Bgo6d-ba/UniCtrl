/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "Pairing.h"

#include "gui/config/ScreenList.h"

#include <QList>

#include <algorithm>
#include <utility>

namespace deskflow::gui::lan {

/**
 * @brief Keeps the computers that joined while the screen layout is being edited until the editing is over
 *
 * The Arrange screens dialog edits a copy of the layout and replaces the layout when it is accepted, which would take
 * away a screen that was added meanwhile. So a computer that joins meanwhile waits, and is added when the dialog has
 * closed, unless the layout has it by then.
 */
class ClientScreenQueue
{
public:
  //! True when the layout has a screen or an alias of this name. The core does not tell names apart by case.
  static bool inLayout(const ScreenList &layout, const QString &screenName)
  {
    const auto isName = [&screenName](const QString &name) {
      return name.compare(screenName, Qt::CaseInsensitive) == 0;
    };
    return std::ranges::any_of(layout, [&isName](const Screen &screen) {
      return isName(screen.name()) || std::ranges::any_of(screen.aliases(), isName);
    });
  }

  //! The layout is being edited. Editing may nest.
  void open()
  {
    ++m_open;
  }

  //! True when the client is to be added now, false when it is kept until the editing is over
  bool submit(const PeerIdentity &client)
  {
    if (m_open == 0)
      return true;
    if (std::ranges::none_of(m_queued, [&client](const PeerIdentity &queued) {
          return queued.screenName.compare(client.screenName, Qt::CaseInsensitive) == 0;
        }))
      m_queued.append(client);
    return false;
  }

  //! The editing ended: once the last one has, the computers kept meanwhile that the layout lacks, oldest first
  QList<PeerIdentity> close(const ScreenList &layout)
  {
    if (m_open == 0 || --m_open > 0)
      return {};
    auto queued = std::exchange(m_queued, {});
    queued.removeIf([&layout](const PeerIdentity &client) { return inLayout(layout, client.screenName); });
    return queued;
  }

private:
  int m_open = 0;
  QList<PeerIdentity> m_queued;
};

} // namespace deskflow::gui::lan
