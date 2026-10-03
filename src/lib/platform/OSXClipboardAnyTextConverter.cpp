/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardAnyTextConverter.h"

#include <algorithm>

//
// OSXClipboardAnyTextConverter
//

IClipboard::Format OSXClipboardAnyTextConverter::getFormat() const
{
  return IClipboard::Format::Text;
}

std::string OSXClipboardAnyTextConverter::fromIClipboard(const std::string &data) const
{
  // convert linefeeds and then convert to desired encoding
  return doFromIClipboard(convertLinefeedToMacOS(data));
}

std::string OSXClipboardAnyTextConverter::toIClipboard(const std::string &data) const
{
  // convert text then newlines
  return convertLinefeedToUnix(doToIClipboard(data));
}

static bool isLF(char ch)
{
  return (ch == '\n');
}

static bool isCR(char ch)
{
  return (ch == '\r');
}

std::string OSXClipboardAnyTextConverter::convertLinefeedToMacOS(const std::string &src)
{
  // note -- we assume src is a valid UTF-8 string
  std::string copy = src;

  std::replace_if(copy.begin(), copy.end(), isLF, '\r');

  return copy;
}

std::string OSXClipboardAnyTextConverter::convertLinefeedToUnix(const std::string &src)
{
  std::string copy = src;

  std::replace_if(copy.begin(), copy.end(), isCR, '\n');

  return copy;
}

std::string OSXClipboardAnyTextConverter::convertString(
    const std::string &data, CFStringEncoding fromEncoding, CFStringEncoding toEncoding
)
{
  CFStringRef stringRef = CFStringCreateWithCString(kCFAllocatorDefault, data.c_str(), fromEncoding);

  if (stringRef == nullptr) {
    return std::string();
  }

  CFIndex buffSize = 0;
  CFRange entireString = CFRangeMake(0, CFStringGetLength(stringRef));

  CFStringGetBytes(stringRef, entireString, toEncoding, 0, false, nullptr, 0, &buffSize);

  std::string result(buffSize, '\0');
  CFStringGetBytes(
      stringRef, entireString, toEncoding, 0, false, reinterpret_cast<uint8_t *>(result.data()), buffSize, nullptr
  );

  CFRelease(stringRef);

  return result;
}
