#ifdef _WIN32
#   include <Windows.h>
#   include <process.h>
#endif
#include "Thread.hpp"
#include "ScopedLock.hpp"

Thread::Thread() :
	m_start_mutex(true),
	m_running(false),
	m_stop_requested(0)
	// m_tid cannot be initialized because when portable_thread_t is a pthread_t, it's an opaque type
{
}

Thread::~Thread()
{
	stop();
}

static PORTABLE_THREAD_FUNC_DECL run(void *args)
{
	Thread* self = static_cast<Thread*>(args);
	self->Run();
	return NULL;
}

RetVal Thread::start()
{
	m_start_mutex.Lock();
	if (!m_running)
	{
		if (portable_thread_create(&m_tid, run, this))
		{
			m_running = false;
			m_start_mutex.Unlock();
			return ReturnCode::THREAD_START;
		}
		m_running = true;
	}
	m_start_mutex.Unlock();
	return ReturnCode::SUCCESS;
}

void Thread::stop()
{
	m_start_mutex.Lock();
	const bool copy_of_m_running = m_running;
	m_start_mutex.Unlock();

	if (copy_of_m_running)
	{
        InterlockedCompareExchange(&m_stop_requested, 1, 0);

		portable_thread_join(m_tid);

		m_start_mutex.Lock();
		m_running = false;
		m_start_mutex.Unlock();
	}
}

bool Thread::running()
{
	// This function is called in many places by a derived class
	// to check to see if its Run method should stop executing.  It
	// should be renamed to stop_requested() or similar
    const unsigned int copy = InterlockedExchangeAdd(&m_stop_requested, 0);
	return copy == 0;
}
