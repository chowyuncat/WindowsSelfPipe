#define _WIN32_WINNT 0x500
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>

#include <stdio.h>

#include "portable_thread.h"

#include "EventManager.hpp"

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
    int yes = 1;
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

#if 1
	unsigned long val = 0;
	::ioctlsocket(fds[0], FIONBIO, &val);
	::ioctlsocket(fds[1], FIONBIO, &val);
#endif
}


static const char kCancelChar = '?';
static const int kCancelLength = 1;

static unsigned int __stdcall threadproc(void *args)
{
    const SOCKET *sockets = reinterpret_cast<const SOCKET*>(args);
    while (true)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockets[0], &fds);
        timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        select(2, &fds, NULL, NULL, &tv);
        if (FD_ISSET(sockets[0], &fds))
        {
            char discard;
            recv(sockets[0], &discard, kCancelLength, 0); // MSG_PEEK is free of delay
            break;
        }
        else
        {
            // printf("woke up with nothing to do\n");
        }
    }

    closesocket(sockets[0]);
    closesocket(sockets[1]);
    return 0;
}

static void simple()
{
    int i;
    for (i = 0; i < 1000; ++i)
    {
        SOCKET sockets[2];
        pair(sockets);
        if (sockets[0] == INVALID_SOCKET || sockets[1] == INVALID_SOCKET)
        {
            fprintf(stderr, "Couldn't create a socket\n");
            exit(1);
        }
        //printf("sockets: %d, %d\n", sockets[0], sockets[1]);

        portable_thread_t thread;
        portable_thread_create(&thread, threadproc, &sockets);

        send(sockets[1], &kCancelChar, kCancelLength, 0);

        portable_thread_join(thread);
    }

    printf("Done with %8d\n", i);
}

int main()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(1,1), &wsaData);

#if 1
    simple();
#endif

    EventManager em;
    em.start();
    Sleep(1);
    em.stop();

    return 0;
}