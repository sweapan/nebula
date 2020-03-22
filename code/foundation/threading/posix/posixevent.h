#pragma once
#ifndef POSIX_POSIXEVENT_H
#define POSIX_POSIXEVENT_H
//------------------------------------------------------------------------------
/**
    @class Posix::PosixEvent

    Posix implmentation of an event synchronization object.

    (C) 2006 Radon Labs GmbH
    (C) 2013-2018 Individual contributors, see AUTHORS file
*/
#include "core/types.h"
#include <semaphore.h>

//------------------------------------------------------------------------------
namespace Posix
{
class PosixEvent
{
public:
    /// constructor
    PosixEvent(bool manualReset=false);
    /// destructor
    ~PosixEvent();
    /// signal the event
    void Signal();
    /// reset the event (only if manual reset)
    void Reset();
    /// wait for the event to become signalled
    void Wait() const;
    /// wait for the event with timeout in millisecs
    bool WaitTimeout(int ms) const;
    /// check if event is signalled
    bool Peek() const;
private:
    sem_t* semaphore;
};

//------------------------------------------------------------------------------
/**
    manual reset is not used, since it's only used for win32events
*/
inline
PosixEvent::PosixEvent(bool manualReset) : 
    semaphore(0)
{
    this->semaphore = new sem_t;
    int res = sem_init(this->semaphore, 0, 0);
    int currentVal;
    res = sem_getvalue(this->semaphore, &currentVal);
    n_printf("Starting %x\n", this->semaphore);
}

//------------------------------------------------------------------------------
/**
*/
inline
PosixEvent::~PosixEvent()
{
    sem_destroy(this->semaphore);
    delete this->semaphore;
    this->semaphore = 0;
}

//------------------------------------------------------------------------------
/**
*/
inline void
PosixEvent::Signal()
{
    sem_post(this->semaphore);
    int currentVal;
    int res = sem_getvalue(this->semaphore, &currentVal);
    n_printf("after %x signal value %d\n", this->semaphore, currentVal);
}

//------------------------------------------------------------------------------
/**
*/
inline void 
PosixEvent::Reset()
{
    // do nothing, not required for POSIX events
    n_printf("Reset %x\n", this->semaphore);
}

//------------------------------------------------------------------------------
/**
*/
inline void
PosixEvent::Wait() const
{
    n_printf("Wait start %x\n", this->semaphore);
    sem_wait(this->semaphore);    
    n_printf("Wait done %x\n", this->semaphore);
}

//------------------------------------------------------------------------------
/**
    Waits for the event to become signaled with a specified timeout
    in milli seconds. If the method times out it will return false,
    if the event becomes signaled within the timeout it will return 
    true.
*/
inline bool
PosixEvent::WaitTimeout(int timeoutInMilliSec) const
{
    timespec now;
    int res = clock_gettime(CLOCK_REALTIME, &now);
    n_assert(res != -1);
    time_t sec = timeoutInMilliSec / 1000;
    now.tv_sec += sec;
    now.tv_nsec += (timeoutInMilliSec - sec*1000) * 1000000;
    bool waited = 0 == sem_timedwait(this->semaphore, &now);
    n_printf("WaitTimeout %x, %d\n", this->semaphore, waited);
    return waited;
}

//------------------------------------------------------------------------------
/**
    This checks if the event is signaled and returnes immediately.
*/
inline bool
PosixEvent::Peek() const
{
    int currentVal;
    int res = sem_getvalue(this->semaphore, &currentVal);
    n_assert(res == 0);
    n_printf("Peek: %x, value: %d\n", this->semaphore, currentVal);
    return currentVal > 0;
}

}; // namespace Posix
//------------------------------------------------------------------------------
#endif

