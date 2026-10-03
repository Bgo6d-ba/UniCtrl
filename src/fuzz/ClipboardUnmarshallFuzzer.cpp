/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Unmarshalls peer clipboard data; whatever is kept must survive a marshall round trip.

#include "FuzzSupport.h"

#include "deskflow/Clipboard.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  deskflow::fuzz::initArchAndLog();
  Clipboard clipboard;
  IClipboard::unmarshall(&clipboard, std::string_view(reinterpret_cast<const char *>(data), size), 0);

  const auto marshalled = clipboard.marshall();
  Clipboard copy;
  copy.unmarshall(marshalled, 0);
  deskflow::fuzz::check(copy.marshall() == marshalled, "marshall round trip changed the clipboard");
  return 0;
}
