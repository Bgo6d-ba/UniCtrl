/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file
 * @brief Contract of the file drag and drop feature (protocol 1.9): types and interfaces shared by the message layer,
 * the transfer engine and the operating system bridges. See docs/dev/protocol_reference.md.
 *
 * A user drags files on the SOURCE computer and crosses to the TARGET computer with the button held. The source offers
 * the files (names and sizes only, never paths), the target asks for each file when the user drops it, and the source
 * streams the bytes in small chunks under a credit window, so mouse and key events on the same connection are not held
 * up. The engine is platform neutral; only IFileDragSource and IFileDragTarget are implemented per operating system.
 */

namespace deskflow::filedrag {

inline constexpr uint32_t kMaxFilesPerOffer = 64;
inline constexpr size_t kMaxNameBytes = 255;
inline constexpr size_t kMaxEntriesBlobBytes = 32 * 1024;
inline constexpr size_t kMaxChunkBytes = 64 * 1024;
inline constexpr uint32_t kMaxCreditPerRequest = 4 * 1024 * 1024;
inline constexpr uint32_t kInitialCredit = 256 * 1024;

//! One offered file: a sanitised base name (never a path) and its size in bytes
struct Entry
{
  std::string name;
  uint64_t size = 0;
  bool operator==(const Entry &) const = default;
};

//! The files a source offers. The id is chosen by the source and is unique on a connection.
struct Offer
{
  uint32_t id = 0;
  std::vector<Entry> entries;
  bool operator==(const Offer &) const = default;
};

//! How a file ended (the status of a file end message)
enum class Status : uint32_t
{
  Ok = 0,        //!< every byte was sent
  ReadError = 1, //!< the source could not read the file
  Changed = 2,   //!< the file changed while it was offered or sent
  Cancelled = 3, //!< the source cancelled
  Refused = 4    //!< unknown offer or file, or a limit was exceeded
};

//! Bounds both sides enforce; the defaults are the defaults of the settings
struct Limits
{
  uint32_t maxFiles = kMaxFilesPerOffer;
  uint64_t maxFileBytes = 4ULL << 30;
  uint64_t maxTotalBytes = 8ULL << 30;
  uint64_t minFreeBytes = 100ULL << 20; //!< that must stay free on the volume of a destination
  int inactivityMs = 30000;             //!< without a byte moving, a transfer is given up
  int offerLifetimeMs = 60000;          //!< an offer nobody asks for is dropped
};

//! Sends file drag messages to the other computer. The protocol 1.9 proxies implement it.
class IPeerChannel
{
public:
  virtual ~IPeerChannel() = default;
  virtual void sendOffer(const Offer &offer) = 0;
  virtual void sendRequest(uint32_t id, uint32_t index, uint32_t credit) = 0;
  virtual void sendData(uint32_t id, uint32_t index, std::string_view data) = 0;
  virtual void sendEnd(uint32_t id, uint32_t index, Status status) = 0;
  virtual void sendCancel(uint32_t id) = 0;
};

//! Receives the file drag messages of the other computer. The transfer engine implements it and the proxies call it,
//! on the thread of the event loop, with messages they have parsed and validated for shape.
class IPeerMessages
{
public:
  virtual ~IPeerMessages() = default;
  virtual void onOffer(const Offer &offer) = 0;
  virtual void onRequest(uint32_t id, uint32_t index, uint32_t credit) = 0;
  virtual void onData(uint32_t id, uint32_t index, std::string_view data) = 0;
  virtual void onEnd(uint32_t id, uint32_t index, Status status) = 0;
  virtual void onCancel(uint32_t id) = 0;
};

/**
 * @brief Operating system bridge of the computer files are dragged FROM
 *
 * Its methods are called on the event loop thread; an implementation hops to the main thread itself when the system
 * needs it.
 */
class IFileDragSource
{
public:
  virtual ~IFileDragSource() = default;
  //! True while the user drags files (not text or images) with the mouse button held
  virtual bool fileDragActive() = 0;
  //! The files of that drag, as a snapshot. Empty when there is none.
  virtual std::vector<std::filesystem::path> draggedFiles() = 0;
  //! Ends the drag on this computer so that nothing is dropped here. The events it posts must not reach the event tap
  //! of this application.
  virtual void cancelLocalDrag() = 0;
};

//! Told by the bridge of a target that the user dropped a promised file and the system chose where it goes.
//! `done` has to be called exactly once, from any thread, whether the file arrived or not.
using FetchDone = std::function<void(bool ok, const std::string &error)>;
using FetchFile = std::function<void(uint32_t index, const std::filesystem::path &destination, FetchDone done)>;

/**
 * @brief Operating system bridge of the computer files are dragged TO
 *
 * Its methods are called on the event loop thread; an implementation hops to the main thread itself when the system
 * needs it. The callbacks it gets may be called from any thread.
 */
class IFileDragTarget
{
public:
  virtual ~IFileDragTarget() = default;
  //! Starts a drag session under the cursor that offers the files of `offer`. `fetch` is called for every file the
  //! user drops. False when a session could not be started.
  virtual bool beginDrop(const Offer &offer, FetchFile fetch) = 0;
  //! Ends the session of offer `id`, dropped or not. Nothing happens when there is none.
  virtual void endDrop(uint32_t id) = 0;
};

} // namespace deskflow::filedrag
