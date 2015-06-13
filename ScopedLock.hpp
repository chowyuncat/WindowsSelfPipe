
#ifndef __SAFETY_LOCK_H__949345
#define __SAFETY_LOCK_H__949345

#include "Lockable.hpp"

class ScopedLock
{
public:
	ScopedLock(const MutexLock* lock)
	{
		_Lock = lock;
		_Lock->Lock();
	}

	~ScopedLock()
	{
		_Lock->Unlock();
	}

protected:
	mutable const MutexLock* _Lock;
};

#endif
