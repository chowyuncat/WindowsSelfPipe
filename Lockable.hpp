

#ifndef __LOCKABLE_H__0376556
#define __LOCKABLE_H__0376556

namespace ReturnCode
{
    const int SUCCESS = 0;
    const int FAILED = 1;

    const int TIMEOUT = 1018;

    const int CANT_LOCK = 2001;
    const int THREAD_START = 2005;

    const int SOCKET_READ = 3001;
}

typedef int RetVal;


#ifdef _WIN32
#   include <Windows.h>
#   include <process.h>
#endif

#ifndef _WIN32
#  include <pthread.h>
#endif


/** A standard Mutex lock
 * The constructor and destructor create and destroy the mutex
 * for the user.
 */
class MutexLock
{
public:
	MutexLock(bool recursive = true);
	virtual ~MutexLock();
	/** Lock the object 
	 */
	RetVal Lock() const;

	/// Unlock the object
	RetVal Unlock() const ;

	/** Attempt to lock the object 
	 */
	RetVal TryLock() const;

protected:
	#ifndef _WIN32
	mutable pthread_mutex_t m_Mutex;
	#else
	mutable CRITICAL_SECTION m_csection;
	mutable HANDLE m_semaphore;
	#endif

	friend class Condition;
};

/** A wait condition
 * Example Usage:
 * 1. Call Lock
 * 2. Initiate some action that will eventually wake us up
 * 3. Call Wait
 * 4. Process the result
 * 5. Unlock
 * In the other thread:
 * 1. Call Lock
 * 2. Do something
 * 3. Call Signal or Broadcast
 * 4. Call Unlock
 */
class Condition : public MutexLock
{
public:
	Condition();
	virtual ~Condition();
	
	/// Wait for a condition to be signal
	void Wait(MutexLock *external = NULL) const;
	
	/** Wait for a condition to be signal with a timeout
	 * Returns TIMEOUT if the wait times out
	 */
	RetVal TimedWait(int sec, MutexLock *external = NULL) const;
	
	/// Wake a single thread waiting
	void Signal(bool external = false) const;
	
	/// Wake all threads waiting
	void Broadcast(bool external = false) const;
	
protected:
	#ifndef _WIN32
	mutable pthread_cond_t m_cond;
	#else
	mutable HANDLE m_semaphore;
	mutable unsigned long m_generation;
	mutable unsigned long m_num_wake;
	mutable unsigned long m_num_waiting;
	#endif
};


#endif
