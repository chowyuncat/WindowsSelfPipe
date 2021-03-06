#ifndef __EVENT_H__34676767
#define __EVENT_H__34676767


#include <vector>

#include "Thread.hpp"
#include "Lockable.hpp"

class EventManager;
class  EventBase
{
	public:
		EventBase() {}
		virtual ~EventBase() {}
};
	
class  FDEventBase : public EventBase
{
	public:
		FDEventBase()
		{
			m_socket = INVALID_SOCKET;
		}
		virtual ~FDEventBase() {}

		virtual void ReadableEvent(EventManager*);
		SOCKET fd() const { return m_socket; };
	protected:
		SOCKET m_socket;
};

class  TimerEventBase : public EventBase
{
public:
	// input is in relative time
	TimerEventBase(long seconds, bool reschedule = false) ;
	virtual ~TimerEventBase();

	virtual RetVal TimerExpired(EventManager*) = 0;
		
	struct timeval& time();
	long seconds() const;
	bool reschedule();
		
protected:
	struct timeval m_tv;	// absolute time
	long m_seconds;			// relative time
	bool m_reschedule;		// does this object reschedule itself?
};

class  EventManager : public Thread
{
public:
	EventManager();
	virtual ~EventManager();
		
	void add(FDEventBase* val);


	void remove(FDEventBase* val);


	virtual RetVal start();
	virtual void stop();
	virtual void Run();

protected:

	typedef std::vector<FDEventBase*> EventList_t;

	EventList_t m_events;

	MutexLock m_lock;
    MutexLock m_pipelock;
	SOCKET m_pipe[2];
}; // EventManager


#endif
