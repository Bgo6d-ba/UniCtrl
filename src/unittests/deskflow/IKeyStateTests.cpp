/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IKeyStateTests.h"

#include "deskflow/IKeyState.h"

#include <memory>

void IKeyStateTests::allocDestination()
{
  const std::unique_ptr<IKeyState::KeyInfo> info(IKeyState::KeyInfo::alloc(1, 2, 3, 4, {"test1", "test2"}));
  QCOMPARE(info->m_screens, ":test1:test2:");
}

void IKeyStateTests::alloc_inEvent_ownedAsDataObject()
{
  Event event(EventTypes::KeyStateKeyDown, nullptr, IKeyState::KeyInfo::alloc(1, 2, 3, 4, {"test1"}));

  QVERIFY(event.getDataObject() != nullptr);
  QVERIFY(event.getData() == nullptr);
  Event::deleteData(event);
}

QTEST_MAIN(IKeyStateTests)
