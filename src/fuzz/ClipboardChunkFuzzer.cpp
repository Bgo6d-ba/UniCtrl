/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Assembles clipboard chunks the way ServerProxy does. The first byte picks the receive limit;
// the rest is a stream of DCLP message bodies.

#include "FuzzSupport.h"

#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/DeskflowException.h"

using deskflow::fuzz::check;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  deskflow::fuzz::initArchAndLog();
  if (size == 0) {
    return 0;
  }
  const size_t maxDataSize = data[0] < 0x80 ? data[0] : 1024 * 1024;
  deskflow::fuzz::MemoryStream stream({reinterpret_cast<const char *>(data) + 1, size - 1});

  std::string cached;
  ClipboardChunkAssemblyState state;
  while (stream.getSize() > 0) {
    ClipboardID id = 0;
    uint32_t sequence = 0;
    TransferState result;
    try {
      result = ClipboardChunk::assemble(&stream, cached, id, sequence, state, maxDataSize);
    } catch (const BadClientException &) {
      // ServerProxy drops the connection
      break;
    }
    switch (result) {
      using enum TransferState;
    case Started:
    case InProgress:
      check(state.active && cached.size() <= state.expectedSize, "cached more than declared");
      break;
    case Finished: {
      check(!state.active && cached.size() == state.expectedSize, "finished with the wrong size");
      Clipboard clipboard;
      clipboard.unmarshall(cached, 0);
      cached.clear();
      break;
    }
    case Error:
      check(!state.active && cached.empty(), "error kept a transfer");
      break;
    default:
      check(cached.empty(), "oversize transfer cached data");
      break;
    }
  }
  return 0;
}
