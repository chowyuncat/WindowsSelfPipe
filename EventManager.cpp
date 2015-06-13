#undef _WIN32_WINNT
#define _WIN32_WINNT 0x500
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <stdio.h>
#define  __PORT_H__0396846
#		define BVTUTIL_EXPORT
#ifndef __GNUC__
	#define EPOCHFILETIME (116444736000000000i64)
	#else
	#define EPOCHFILETIME (116444736000000000LL)
	#endif
static int gettimeofday(struct timeval *tv, struct timezone* unused)
	{
		FILETIME        ft;
		LARGE_INTEGER   li;
		__int64         t;

		(void) unused;

		if (tv)
		{
			/* WARNING: This function's granularity is quite coarse, especially on Windows XP,
			   where the time may update only every 10-15 milliseconds.
			   TODO: Use a timer with finer granularity */
			GetSystemTimeAsFileTime(&ft);
			li.LowPart  = ft.dwLowDateTime;
			li.HighPart = ft.dwHighDateTime;
			t  = li.QuadPart;       /* In 100-nanosecond intervals */
			t -= EPOCHFILETIME;     /* Offset to the Epoch time */
			t /= 10;                /* In microseconds */
			tv->tv_sec  = (long)(t / 1000000);
			tv->tv_usec = (long)(t % 1000000);
		}

		return 0;
	}

#include <errno.h>

#include "EventManager.hpp"
#include "Lockable.hpp"
#include "ScopedLock.hpp"

#include <algorithm>
#include <string.h>

#define BVT_LOG printf


///////////////////////////////////////////////////////////////////////////////
// input is in relative time
TimerEventBase::TimerEventBase(long seconds, bool reschedule) 
{
	m_seconds = seconds;
	m_reschedule = reschedule;

	gettimeofday(&m_tv, NULL);

	// switch from relative to absolute
	m_tv.tv_sec  += m_seconds;
	m_tv.tv_usec = 0;
}

TimerEventBase::~TimerEventBase()
{
}

struct timeval& TimerEventBase::time()
{
	return m_tv;
}

long TimerEventBase::seconds() const
{
	return m_tv.tv_sec;
}

bool TimerEventBase::reschedule()
{
	if (m_reschedule)
	{
		::gettimeofday(&m_tv, NULL);

		// switch from relative to absolute
		m_tv.tv_sec  += m_seconds;
		m_tv.tv_usec = 0;
	}

	return m_reschedule;
}

bool operator<(const TimerEventBase& x, const TimerEventBase& y)
{
	return (x.seconds() < y.seconds()) ? true : false;
}


///////////////////////////////////////////////////////////////////////////////
void FDEventBase::ReadableEvent(EventManager*)
{
}

///////////////////////////////////////////////////////////////////////////////

static inline void pair(SOCKET fds[2])
{
	// TODO: consider socketpair on Linux
	struct sockaddr_in inaddr;
	struct sockaddr addr;
#ifdef _WIN32
	const int domain = AF_INET;
#else
	const int domain = AF_LOCAL;
#endif
	const SOCKET listener = socket(domain, SOCK_STREAM, 0);
	memset(&inaddr, 0, sizeof(inaddr));
	memset(&addr, 0, sizeof(addr));
	inaddr.sin_family = domain;
	inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	inaddr.sin_port = 0;
	const int yes = 1;
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
	bind(listener, (struct sockaddr *)&inaddr, sizeof(inaddr));
	listen(listener, 1);
	socklen_t len = sizeof(inaddr);
	getsockname(listener, &addr, &len);
	fds[0] = socket(domain, SOCK_STREAM, 0);
	connect(fds[0], &addr, len);
	fds[1] = accept(listener, 0, 0);
	closesocket(listener);

	struct linger dontlinger;
	dontlinger.l_onoff = 1;
	dontlinger.l_linger = 0;
	setsockopt(fds[0], SOL_SOCKET, SO_LINGER, (const char*)&dontlinger, sizeof(dontlinger));
	setsockopt(fds[1], SOL_SOCKET, SO_LINGER, (const char*)&dontlinger, sizeof(dontlinger));
}

EventManager::EventManager()
{
	pair(m_pipe);
}

EventManager::~EventManager()
{
	// TODO: need stop(); ?
}

void EventManager::add(FDEventBase* val)
{
	if( val == NULL )
		return;
	
	ScopedLock _(&m_lock);
	m_events.push_back(val);
}

void EventManager::add(TimerEventBase* val)
{
	if( val == NULL )
		return;

	ScopedLock _(&m_lock);
	m_timers.push_back(val);
	sort(m_timers.begin(), m_timers.end());
}

void EventManager::remove(TimerEventBase* val)
{
	if( val == NULL )
		return;

	ScopedLock _(&m_lock);
	TimerList_t::iterator i = m_timers.begin();
	while (i != m_timers.end())
	{
		TimerList_t::iterator j = i;
		i++;

		if ((*j) == val)
		{
			m_timers.erase(j);
			break;
		}
	}
}

void EventManager::remove(FDEventBase* val)
{
	if( val == NULL )
		return;

	ScopedLock _(&m_lock);
	EventList_t::iterator i = m_events.begin();
	while (i != m_events.end())
	{
		EventList_t::iterator j = i;
		i++;

		if ((*j) == val)
		{
			m_events.erase(j);
			break;
		}
	}
}

#ifdef _WIN32
static void __stdcall UserAPC_CloseSocket(ULONG_PTR userData)
{
    SOCKET *sock = reinterpret_cast<SOCKET*>(userData);
    SOCKET s = *sock;
    closesocket(s);
    *sock = INVALID_SOCKET;
}
#endif

static const char kCancelChar = '?';
static const int kCancelLength = 1;

RetVal EventManager::start()
{
    if (m_pipe[0] == INVALID_SOCKET || m_pipe[1] == INVALID_SOCKET)
    {
        return ReturnCode::FAILED;
    }
    return Thread::start();
}

void EventManager::stop()
{
	m_start_mutex.Lock();
	const bool local_running = m_running;
	m_start_mutex.Unlock();
#ifdef _WIN32
    // HANDLE threadHandle = Thread::m_tid;
    // ULONG_PTR userData = reinterpret_cast<ULONG_PTR>(&m_cancelSocket);
    // QueueUserAPC(UserAPC_CloseSocket, threadHandle, userData);
#endif
    // NB: if you called closesocket on m_cancelSocket's FD it could be immediately reused
    // Furthermore, assigning INVALID_SOCKET to m_cancelSocket is a race
	if (local_running)
	{
        printf("Writing to cancel\n");
		int sent = send(m_pipe[1], &kCancelChar, kCancelLength, 0);
        printf("Wrote %d to cancel\n", sent);

	}

	// NB: Thread::stop blocks, so we have to issue the socket-based cancellation beforehand.
	// This means that the event loop can busy spin while Thread::running() keeps returning true!
	// TODO: Add Thread::beginStop() and move before cancellation.
	Thread::stop();
}

#ifdef _WIN32
#  define SOCKETMAX(a, b) 1
#else
#  define SOCKETMAX(a, b) MAX((a), (b))
#endif

void EventManager::Run()
{
	RetVal status = ReturnCode::SUCCESS;
    int a = 0;
	while( running() && (status == ReturnCode::SUCCESS) )
	{
		m_lock.Lock();
		
		// set fds bits and determine highest file descriptor
		fd_set fds;
		FD_ZERO(&fds);
		int fd_max = 0;

		for (EventList_t::iterator i = m_events.begin(); i != m_events.end(); i++)
		{
			const FDEventBase* ev = *i;
			if (ev == NULL) // TODO: How can ev be NULL?
				continue;
			const SOCKET fd = ev->fd();
			if (fd == INVALID_SOCKET)
				continue;
			fd_max = SOCKETMAX(fd_max, fd);
            // HACK:
			// HACK: FD_SET(fd, &fds);
		}

		// determine the relative time for the oldest item in the time queue.
		// TODO: HACK: this loop will wake once a second even if no events are ready
		struct timeval tv;
		if (m_timers.empty())
		{
			tv.tv_sec = 0;
		}
		else
		{
			struct timeval tmp_tv;
			::gettimeofday(&tmp_tv, NULL);
			//BVT_LOG(LOG_DEBUG, "expires: %d now: %d", m_timers[0]->seconds(), tmp_tv.tv_sec);
			if( tmp_tv.tv_sec < m_timers[0]->seconds() )
				tv.tv_sec = 0; //m_timers[0]->seconds() - tmp_tv.tv_sec;
			else
				tv.tv_sec = 0;
		}
		//BVT_LOG(LOG_DEBUG, "tv_sec:%d", tv.tv_sec);
		tv.tv_sec = 5;
		tv.tv_usec = 500 * 1000;
		
		m_lock.Unlock();

		FD_SET(m_pipe[0], &fds);
		fd_max = SOCKETMAX(fd_max, m_pipe[0]);
		// Windows' select MUST have a valid fd, but the first (fd_max) parameter is ignored
		const int ret = select(fd_max + 1, &fds, NULL, NULL, &tv);

		m_lock.Lock();

		switch(ret)
		{
			case 0: // timedout
				//BVT_LOG(LOG_TRACE, "EventManager - select timeout");
				processTimers();
				status = ReturnCode::SUCCESS;
				break;

			case -1: // error
				//BVT_LOG(LOG_DEBUG, "EventManager - select returned -1 %d %s", errno, strerror(errno));
				if (errno != EINTR)
					status = ReturnCode::SOCKET_READ;
				break;

			default: // data 
				status = processTimers();

				for (EventList_t::iterator i = m_events.begin(); i != m_events.end(); i++)
				{
					SOCKET fd = (*i)->fd();
					if (fd != INVALID_SOCKET && FD_ISSET(fd, &fds)) // NB: fd could be zero and valid if stdin is closed before a socket is created!
					{
						//BVT_LOG(LOG_DEBUG, "EventManager - got data on fd %d", (*i)->fd() );
						(*i)->ReadableEvent(this);
					}
				}

				if (FD_ISSET(m_pipe[0], &fds))
				{
					char discard;
                    LARGE_INTEGER start, stop;
                    QueryPerformanceCounter(&start);
					if (kCancelLength != recv(m_pipe[0], &discard, kCancelLength, 0))
					{
                        a= 2;
						BVT_LOG("EventManager cancel socket error\n");
					}
                    else
                    {
                        a = 1;
                    }
                    //Sleep(1);
                    QueryPerformanceCounter(&stop);
                    a = (int)(stop.QuadPart - start.QuadPart);

					break;
				}
		}
        // Sleep(1);
		m_lock.Unlock();

	}
    printf("a:%d\n", a);

	closesocket(m_pipe[0]);
	closesocket(m_pipe[1]);
	// if( status != BVT::ReturnCode::SUCCESS )
		BVT_LOG("EventManager::Run() exiting: retval:%d errno:%d", status, errno);
}

RetVal  EventManager::processTimers()
{
	RetVal status = ReturnCode::SUCCESS;

    struct timeval  tv;
	::gettimeofday(&tv, NULL);

	bool resort = false;
	//tv.tv_sec  -= m_timers[0]->time().tv_sec;

	for (TimerList_t::iterator i = m_timers.begin(); i != m_timers.end() ; i++)
	{
//		BVT_LOG(LOG_DEBUG, "%d %d", tv.tv_sec, (*i)->seconds());
		if( tv.tv_sec < (*i)->seconds() )
			break;

		TimerList_t::iterator j = i;

		//BVT_LOG(LOG_DEBUG, "Timer expired");
		RetVal tstatus = (*j)->TimerExpired(this);
		if (!(*j)->reschedule())
		{
			resort = true;
			m_timers.erase(j);
		}

		// feedback the first failed callback
		if (status == ReturnCode::SUCCESS && 
				tstatus != ReturnCode::SUCCESS) status = tstatus;
	}

	if( resort )
		sort(m_timers.begin(), m_timers.end());
	return status;
}

