#include "portable_thread.h"
int portable_thread_create(portable_thread_t *t, unsigned int (__stdcall * run_proc)(void *), void *args)
{
    const unsigned int kDefaultStackSize = 0;
    uintptr_t ptr_handle = _beginthreadex(NULL, kDefaultStackSize, run_proc, args, 0, NULL);
    if (0 == ptr_handle)
        return 1;

    *t = (HANDLE) ptr_handle;
    return 0;
}

void portable_thread_join(portable_thread_t t)
{
    HANDLE handle = t;
#ifdef _DEBUG
    if (handle == GetCurrentThread())
    {
        // Deadlock!
        DebugBreak();
    }
#endif
    if (WAIT_OBJECT_0 != WaitForSingleObject(handle, INFINITE))
    {
        DebugBreak();
    }
    if (FALSE == CloseHandle(handle))
    {
        DebugBreak();
    }
}

portable_thread_t portable_thread_self()
{
    // NB: this HANDLE cannot be passed to other threads!
    // It is a pseudo-handle valid only for the current thread.
    HANDLE handle = GetCurrentThread();
    return handle;
}
