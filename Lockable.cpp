
#ifdef _WIN32
#   include <Windows.h>
#   include <process.h>
#endif
#include "Lockable.hpp"
#include <limits.h>

#ifndef _WIN32

#include <pthread.h>
#include <stdio.h>

#if defined(_WIN32) || defined(__APPLE__)
#  define CONDITION_USES_UNRELIABLE_CLOCK
   // The clock used may be adjusted by NTP or other system clock
   // services, adjusting timeouts to be in the past or far in the future
#else
#  define CONDITION_USES_MONOTONIC_CLOCK
#endif


MutexLock::MutexLock(bool recursive)
{
	pthread_mutexattr_t MutexAttr;
	pthread_mutexattr_init(&MutexAttr);
	if (recursive)
		pthread_mutexattr_settype(&MutexAttr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_Mutex, &MutexAttr);
	pthread_mutexattr_destroy(&MutexAttr);
}

MutexLock::~MutexLock()
{
	pthread_mutex_destroy(&m_Mutex);
}


/** Lock the object 
 */
BVT::RetVal MutexLock::Lock() const
{
	if(pthread_mutex_lock(&m_Mutex))
		return ReturnCode::CANT_LOCK;
	return ReturnCode::SUCCESS;
}

/// Unlock the object
BVT::RetVal MutexLock::Unlock() const
{
	if(pthread_mutex_unlock(&m_Mutex))
		return ReturnCode::CANT_LOCK;
	return ReturnCode::SUCCESS;
}

/** Attempt to lock the object 
 */
BVT::RetVal MutexLock::TryLock() const
{
	if(pthread_mutex_trylock(&m_Mutex))
		return ReturnCode::CANT_LOCK;
	return ReturnCode::SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
Condition::Condition() : MutexLock(false)
{
// On unix, pthread_cond defaults to using CLOCK_REALTIME for timeout counters,
// but this is bad... whenever the system clock is set ahead during a timed
// wait, it triggers an erroneous timeout. The fix is to use CLOCK_MONOTONIC, 
// but the notion of setting the clock does not exist (yet) for Windows pthreads.

// TODO: Can we figure out a way to avoid conditional compilation here?
#if defined CONDITION_USES_UNRELIABLE_CLOCK
	pthread_cond_init(&m_cond, NULL);
#elif defined CONDITION_USES_MONOTONIC_CLOCK
	pthread_condattr_t attr;
	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	pthread_cond_init( &m_cond, &attr );
#else
	#error A clock source for Condition must be defined
#endif
}

Condition::~Condition()
{
	Broadcast();
	pthread_cond_destroy( &m_cond );
}

/// Wait for a condition to be signal
void Condition::Wait(MutexLock *external) const
{
	pthread_mutex_t *the_mutex;
	if (external != NULL)
		the_mutex = &external->m_Mutex;
	else
		the_mutex = &this->m_Mutex;

	pthread_cond_wait(&m_cond, the_mutex);
}

/** Wait for a condition to be signal with a timeout
 * Returns TIMEOUT if the wait times out
 */
BVT::RetVal Condition::TimedWait(int sec, MutexLock *external) const
{
	pthread_mutex_t *the_mutex;
	if (external != NULL)
		the_mutex = &external->m_Mutex;
	else
		the_mutex = &this->m_Mutex;

	struct timespec ts;
#if defined CONDITION_USES_UNRELIABLE_CLOCK
	ts.tv_sec = static_cast<long>(time(NULL) + sec);
	ts.tv_nsec = 0;
#elif defined CONDITION_USES_MONOTONIC_CLOCK
	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_sec += sec;
#else
	#error A clock source for Condition must be defined
#endif

	if( pthread_cond_timedwait(&m_cond, the_mutex, &ts) )
		return BVT::ReturnCode::TIMEOUT;
	else
		return BVT::ReturnCode::SUCCESS;
}

/// Wake a single thread waiting
void Condition::Signal(bool external) const
{
	if (!external)
		Lock();
	pthread_cond_signal(&m_cond);
	if (!external)
		Unlock();
}

/// Wake all threads waiting
void Condition::Broadcast(bool external) const
{
	if (!external)
		Lock();
	pthread_cond_broadcast(&m_cond);
	if (!external)
		Unlock();
}

#else // WIN32 

MutexLock::MutexLock(bool recursive)
{
    if (recursive)
    {
        InitializeCriticalSection(&m_csection);
        m_semaphore = INVALID_HANDLE_VALUE;
    }
    else
    {
        // TODO: This is a heavier kernel object than it needs to be.
        // Consider an auto-reset event instead.
        // TODO: Only create a non-recursive mutex in some sort of checked build where you need to deadlock
        // in case of accidental recursion.
        // Any correct non-recursive implementation will also be correct under a recursive implementation
        m_semaphore = CreateSemaphore(NULL, 1, 1, NULL);
    }
}

MutexLock::~MutexLock()
{
    if (m_semaphore == INVALID_HANDLE_VALUE)
        DeleteCriticalSection(&m_csection);
    else
        CloseHandle(m_semaphore);
}

RetVal MutexLock::Lock() const
{
    if (m_semaphore == INVALID_HANDLE_VALUE)
        EnterCriticalSection(&m_csection);
    else
        WaitForSingleObject(m_semaphore, INFINITE);
    return ReturnCode::SUCCESS;
}

RetVal MutexLock::Unlock() const
{
    if (m_semaphore == INVALID_HANDLE_VALUE)
        LeaveCriticalSection(&m_csection);
    else
        ReleaseSemaphore(m_semaphore, 1, NULL);
    return ReturnCode::SUCCESS;
}

RetVal MutexLock::TryLock() const
{
    if (m_semaphore == INVALID_HANDLE_VALUE)
    {
        if (FALSE == TryEnterCriticalSection(&m_csection))
            return ReturnCode::CANT_LOCK;
        else
            return ReturnCode::SUCCESS;
    }
    else
    {
        if (WAIT_OBJECT_0 != WaitForSingleObject(m_semaphore, 0))
            return ReturnCode::CANT_LOCK;
        else
            return ReturnCode::SUCCESS;
    }
}

Condition::Condition() :
    m_num_waiting(0),
    m_num_wake(0),
    m_generation(0)
{
    this->m_semaphore = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    InitializeCriticalSection(&this->m_csection);
}

Condition::~Condition()
{
    CloseHandle(this->m_semaphore);
    DeleteCriticalSection(&this->m_csection);
}

RetVal Condition::TimedWait(int sec, MutexLock *external) const
{
    DWORD timeout_ms = sec * 1000;
    DWORD res;
    RetVal rv;
    unsigned int wake = 0;
    unsigned long generation;

    EnterCriticalSection(&this->m_csection);
    this->m_num_waiting++;
    generation = this->m_generation;
    LeaveCriticalSection(&this->m_csection);

    if (external == NULL)
        Unlock(); // normally an externally owned mutex is passed to this function.
    else
        external->Unlock();

    do
    {
        // TODO: Decrement the timeout if this thread sleeps again
        res = WaitForSingleObject(this->m_semaphore, timeout_ms);

        EnterCriticalSection(&this->m_csection);
        if (this->m_num_wake)
        {
            if (this->m_generation != generation)
            {
                this->m_num_wake--;
                this->m_num_waiting--;
                rv = ReturnCode::SUCCESS;
                break;
            }
            else
            {
                wake = 1;
            }
        }
        else if (res != WAIT_OBJECT_0)
        {
            this->m_num_waiting--;
            rv = ReturnCode::TIMEOUT;
            break;
        }
        LeaveCriticalSection(&this->m_csection);

        if (wake)
        {
            wake = 0;
            ReleaseSemaphore(this->m_semaphore, 1, NULL);
        }
    } while (1);

    LeaveCriticalSection(&this->m_csection);

    if (external == NULL)
        Lock(); // normally an externally owned mutex is passed to this function.
    else
        external->Lock();

    return rv;
}

void Condition::Wait(MutexLock *external) const
{
    TimedWait(INFINITE, external);
}

void Condition::Signal(bool external) const
{
     if (!external)
        Lock(); // normally an externally owned mutex is passed to this function.
  
    unsigned int wake = 0;

    EnterCriticalSection(&this->m_csection);
    if (this->m_num_waiting > this->m_num_wake)
    {
        wake = 1;
        this->m_num_wake++;
        this->m_generation++;
    }
    LeaveCriticalSection(&this->m_csection);

    if (wake)
    {
        ReleaseSemaphore(this->m_semaphore, 1, NULL);
    }

    if (!external)
        Unlock(); // normally an externally owned mutex is passed to this function.
}

void Condition::Broadcast(bool external) const
{
    if (!external)
        Lock(); // normally an externally owned mutex is passed to this function.

    unsigned long num_wake = 0;

    EnterCriticalSection(&this->m_csection);
    if (this->m_num_waiting > this->m_num_wake)
    {
        num_wake = this->m_num_waiting - this->m_num_wake;
        this->m_num_wake = this->m_num_waiting;
        this->m_generation++;
    }
    LeaveCriticalSection(&this->m_csection);

    if (num_wake)
    {
        ReleaseSemaphore(this->m_semaphore, num_wake, NULL);
    }

    if (!external)
        Unlock();
}

#endif
