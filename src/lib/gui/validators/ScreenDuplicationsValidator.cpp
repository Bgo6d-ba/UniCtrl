/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2021 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenDuplicationsValidator.h"
#include "gui/config/ComputerList.h"

namespace validators {

ScreenDuplicationsValidator::ScreenDuplicationsValidator(
    const QString &message, const QString &defaultName, const ComputerList *screens
)
    : IStringValidator(message),
      m_defaultName(defaultName),
      m_screenList(screens)
{
  // do nothing
}

bool ScreenDuplicationsValidator::validate(const QString &input) const
{
  bool result = true;

  if (m_screenList) {
    for (const auto &screen : (*m_screenList)) {
      if (!screen.isNull() && !screen.isServer() && input != m_defaultName && input == screen.name()) {
        result = false;
        break;
      }
    }
  }

  return result;
}

} // namespace validators
