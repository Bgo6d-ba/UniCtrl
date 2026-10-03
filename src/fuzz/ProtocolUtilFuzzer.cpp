/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Reads the input with one protocol message format, picked by its first byte. A message that reads
// must write back as exactly the bytes it was read from.

#include "FuzzSupport.h"

#include "deskflow/DeskflowException.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <array>
#include <tuple>

using deskflow::fuzz::MemoryStream;

namespace {

template <const char *const &Fmt, typename... T> void readAndWriteBack(std::string_view data)
{
  MemoryStream in(data);
  std::tuple<T...> values;
  bool read = false;
  try {
    read = std::apply([&in](T &...v) { return ProtocolUtil::readf(&in, Fmt, &v...); }, values);
  } catch (const BadClientException &) {
    return;
  }
  if (!read) {
    return;
  }

  MemoryStream out;
  std::apply([&out](const T &...v) { ProtocolUtil::writef(&out, Fmt, v...); }, values);
  deskflow::fuzz::check(out.bytes() == data.substr(0, data.size() - in.getSize()), Fmt);
}

constexpr const char *kFuzzVec1 = "%1I";
constexpr const char *kFuzzVec2 = "%2I";

using Reader = void (*)(std::string_view);

// clang-format off
constexpr std::array<Reader, 39> kReaders = {
    readAndWriteBack<kMsgHello, std::string, int16_t, int16_t>,
    readAndWriteBack<kMsgHelloBack, std::string, int16_t, int16_t, std::string>,
    readAndWriteBack<kMsgCNoop>,
    readAndWriteBack<kMsgCClose>,
    readAndWriteBack<kMsgCEnter, int16_t, int16_t, uint32_t, uint16_t>,
    readAndWriteBack<kMsgCLeave>,
    readAndWriteBack<kMsgCClipboard, uint8_t, uint32_t>,
    readAndWriteBack<kMsgCScreenSaver, uint8_t>,
    readAndWriteBack<kMsgCResetOptions>,
    readAndWriteBack<kMsgCInfoAck>,
    readAndWriteBack<kMsgCKeepAlive>,
    readAndWriteBack<kMsgDKeyDown, uint16_t, uint16_t, uint16_t, std::string>,
    readAndWriteBack<kMsgDKeyDown1_1, uint16_t, uint16_t, uint16_t>,
    readAndWriteBack<kMsgDKeyDown1_0, uint16_t, uint16_t>,
    readAndWriteBack<kMsgDKeyRepeat, uint16_t, uint16_t, uint16_t, uint16_t, std::string>,
    readAndWriteBack<kMsgDKeyRepeat1_1, uint16_t, uint16_t, uint16_t, uint16_t>,
    readAndWriteBack<kMsgDKeyRepeat1_0, uint16_t, uint16_t, uint16_t>,
    readAndWriteBack<kMsgDKeyUp, uint16_t, uint16_t, uint16_t>,
    readAndWriteBack<kMsgDKeyUp1_0, uint16_t, uint16_t>,
    readAndWriteBack<kMsgDMouseDown, int8_t>,
    readAndWriteBack<kMsgDMouseUp, int8_t>,
    readAndWriteBack<kMsgDMouseMove, int16_t, int16_t>,
    readAndWriteBack<kMsgDMouseRelMove, int16_t, int16_t>,
    readAndWriteBack<kMsgDMouseWheel, int16_t, int16_t>,
    readAndWriteBack<kMsgDMouseWheel1_0, int16_t>,
    readAndWriteBack<kMsgDClipboard, uint8_t, uint32_t, uint8_t, std::string>,
    readAndWriteBack<kMsgDInfo, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t>,
    readAndWriteBack<kMsgDSetOptions, std::vector<uint32_t>>,
    readAndWriteBack<kMsgDFileTransfer, uint8_t, std::string>,
    readAndWriteBack<kMsgDDragInfo, uint16_t, std::string>,
    readAndWriteBack<kMsgDSecureInputNotification, std::string>,
    readAndWriteBack<kMsgDLanguageSynchronisation, std::string>,
    readAndWriteBack<kMsgQInfo>,
    readAndWriteBack<kMsgEIncompatible, int16_t, int16_t>,
    readAndWriteBack<kMsgEBusy>,
    readAndWriteBack<kMsgEUnknown>,
    readAndWriteBack<kMsgEBad>,
    // the vector widths no message uses yet
    readAndWriteBack<kFuzzVec1, std::vector<uint8_t>>,
    readAndWriteBack<kFuzzVec2, std::vector<uint16_t>>,
};
// clang-format on

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  deskflow::fuzz::initArchAndLog();
  if (size == 0) {
    return 0;
  }
  const std::string_view bytes(reinterpret_cast<const char *>(data), size);
  kReaders[data[0] % kReaders.size()](bytes.substr(1));
  return 0;
}
