#ifndef __THREAD_H__34676767
#define __THREAD_H__34676767

typedef int RetVal;

#include "Lockable.hpp"
#include "portable_thread.h"

class Runnable
{
public:
    virtual ~Runnable()
    {}
    virtual void Run() = 0;
};

class Thread : public Runnable
{
public:
    Thread();
    virtual ~Thread();

    virtual RetVal start();
    virtual void stop();
    bool running();

protected:
    MutexLock m_start_mutex;
    MutexLock m_stop_mutex;
    portable_thread_t m_tid;
    bool m_running;
    bool m_stop_requested;

};
#endif
