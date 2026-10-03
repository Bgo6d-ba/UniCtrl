/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "EventTypes.h"

#include <assert.h>
#include <cstdlib>
#include <type_traits>

using deskflow::EventTypes;

class EventData
{
public:
  EventData() = default;
  virtual ~EventData() = default;
};

class ExitEventData : public EventData
{
public:
  explicit ExitEventData(int exitCode) : m_exitCode(exitCode) {};
  ~ExitEventData() override = default;
  int exitCode() const
  {
    return m_exitCode;
  }

private:
  int m_exitCode;
};

//! Event
/*!
 \c Event holds an event type and a pointer to event data. It is movable, but not copyable
*/
class Event
{
public:
  using Flags = uint32_t;
  struct EventFlags
  {
    inline static const Flags NoFlags = 0x00;            //!< No flags
    inline static const Flags DeliverImmediately = 0x01; //!< Dispatch and free event immediately
    inline static const Flags DontFreeData = 0x02;       //!< Don't free data in deleteData
  };

  Event() = default;
  Event(const Event &) = delete;
  Event(Event &&other) = default;
  Event &operator=(const Event &) = delete;
  Event &operator=(Event &&) = default;

  //! Create \c Event, optionally owning \p dataObject
  /*!
  \p target is the intended recipient of the event.
  \p dataObject is deleted along with the event unless \p flags has
  \c DontFreeData. Any payload with a constructor or destructor must
  derive from \c EventData and use this constructor.
  */
  explicit Event(
      EventTypes type, void *target = nullptr, EventData *dataObject = nullptr, Flags flags = EventFlags::NoFlags
  )
      : m_type(type),
        m_target(target),
        m_flags(flags),
        m_dataObject(dataObject)
  {
    // do nothing
  }

  //! Create \c Event with POD data
  /*!
  \p data must be allocated by malloc() because it is released with
  free(), or be flagged \c DontFreeData (or be a \c System/\c Timer
  event, whose data is never freed). Types with a non-trivial destructor
  are rejected at compile time; derive those from \c EventData instead.
  */
  template <typename T>
    requires(!std::is_base_of_v<EventData, T>)
  Event(EventTypes type, void *target, T *data, Flags flags = EventFlags::NoFlags)
      : m_type(type),
        m_target(target),
        m_data(data),
        m_flags(flags)
  {
    static_assert(
        std::is_void_v<T> || std::is_trivially_destructible_v<T>,
        "event data with a destructor must derive from EventData"
    );
  }

  //! @name manipulators
  //@{

  //! Release event data
  /*!
  Releases POD data with free() and deletes the data object.
  */
  static void deleteData(const Event &event)
  {
    switch (event.getType()) {
      using enum EventTypes;
    case Unknown:
    case System:
    case Timer:
      break;

    default:
      if ((event.getFlags() & EventFlags::DontFreeData) == 0) {
        free(event.getData());
        delete event.getDataObject();
      }
      break;
    }
  }

  //! Set data (non-POD)
  /*!
  Set non-POD (non plain old data), where delete is called when the event
  is deleted, and the destructor is called.
  */
  void setDataObject(EventData *dataObject)
  {
    assert(m_dataObject == nullptr);
    m_dataObject = dataObject;
  };

  //@}
  //! @name accessors
  //@{

  //! Get event type
  /*!
  Returns the event type.
  */
  EventTypes getType() const
  {
    return m_type;
  }

  //! Get the event target
  /*!
  Returns the event target.
  */
  void *getTarget() const
  {
    return m_target;
  }

  //! Get the event data (POD).
  /*!
  Returns the event data (POD).
  */
  void *getData() const
  {
    return m_data;
  }

  //! Get the event data (non-POD)
  /*!
  Returns the event data (non-POD). The difference between this and
  \c getData() is that when delete is called on this data, so non-POD
  (non plain old data) dtor is called.
  */
  EventData *getDataObject() const
  {
    return m_dataObject;
  }

  //! Get event flags
  /*!
  Returns the event flags.
  */
  Flags getFlags() const
  {
    return m_flags;
  }

  //@}

private:
  EventTypes m_type = EventTypes::Unknown;
  void *m_target = nullptr;
  void *m_data = nullptr;
  Flags m_flags = EventFlags::NoFlags;
  EventData *m_dataObject = nullptr;
};
