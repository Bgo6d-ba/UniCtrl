/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Red Hat, Inc.
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardTypes.h"

#include <mutex>

namespace deskflow {

//! EI/Portal clipboard implementation
/*!
In-memory clipboard cache for EI (libei) and portal-based input capture.
Portal callbacks use it on the glib thread while the event thread copies it,
so whole-clipboard reads and writes go through copyFrom() and copyTo().
*/
class EiClipboard : public Clipboard
{
public:
  explicit EiClipboard(ClipboardID id) : m_id(id)
  {
  }
  ~EiClipboard() override = default;

  //! Get clipboard ID
  ClipboardID getID() const
  {
    return m_id;
  }

  bool copyFrom(const IClipboard *src)
  {
    std::scoped_lock lock{m_copyMutex};
    return IClipboard::copy(this, src);
  }

  bool copyTo(IClipboard *dst) const
  {
    std::scoped_lock lock{m_copyMutex};
    return IClipboard::copy(dst, this);
  }

private:
  ClipboardID m_id;
  mutable std::mutex m_copyMutex;
};

} // namespace deskflow
