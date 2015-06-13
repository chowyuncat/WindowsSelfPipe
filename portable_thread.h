#ifdef _WIN32
#   include <Windows.h>
#   include <process.h>
    typedef struct portable_thread_t_impl_t * portable_thread_t;
    typedef unsigned int (__stdcall *portable_thread_func_t)(void *);
#   define PORTABLE_THREAD_FUNC_DECL unsigned int __stdcall
#else /* _WIN32 */
#   include <pthread.h>
    typedef pthread_t portable_thread_t;
    typedef void* (*portable_thread_func_t)(void *);
#   define PORTABLE_THREAD_FUNC_DECL void *
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* On success, returns 0. */
int portable_thread_create(portable_thread_t *t, portable_thread_func_t run_proc, void *args);

void portable_thread_join(portable_thread_t t);

portable_thread_t portable_thread_self();

#if defined(__cplusplus)
}
#endif
