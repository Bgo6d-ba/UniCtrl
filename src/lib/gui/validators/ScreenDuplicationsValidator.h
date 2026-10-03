/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2021 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IStringValidator.h"

class ScreenList;

namespace validators {

class ScreenDuplicationsValidator : public IStringValidator
{
  const QString m_defaultName;
  const ScreenList *m_screenList = nullptr;

public:
  ScreenDuplicationsValidator(const QString &message, const QString &defaultName, const ScreenList *screens);
  bool validate(const QString &input) const override;
};

} // namespace validators
