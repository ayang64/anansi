/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://mozilla.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 * 
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */


/* 
 * binder.c --
 *
 * Support for pre-bound privileged ports.
 */

#include "nsd.h"

#ifndef _WIN32
#include <sys/un.h>
#include <sys/uio.h>
#endif

#ifdef HAVE_CMMSG

/*
 * Some platforms use BSD4.4 style message passing.  This structure is used
 * to pass the file descriptor between parent and slave.  Note that
 * the first 3 elements must match those of the struct cmsghdr.
 */

typedef struct CMsg {
    unsigned int len;
    int          level;
    int          type;
    int          fds[1];
} CMsg;

#endif

#define REQUEST_SIZE (sizeof(int) + sizeof(int) + sizeof(int) + 64)
#define RESPONSE_SIZE (sizeof(int))

/*
 * Local variables defined in this file
 */

static Ns_Mutex      lock;
static Tcl_HashTable preboundTcp;
static Tcl_HashTable preboundUdp;
static Tcl_HashTable preboundRaw;
static Tcl_HashTable preboundUnix;

static int binderRunning = 0;
static int binderRequest[2] = { -1, -1 };
static int binderResponse[2] = { -1, -1 };

/*
 * Local functions defined in this file
 */

static void PreBind(char *line);
static void Binder(void);

NS_RCSID("@(#) $Header$");

#ifndef _WIN32

/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListenEx --
 *
 *      Create a new TCP socket bound to the specified port and
 *      listening for new connections.
 *
 * Results:
 *      Socket descriptor or -1 on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockListenEx(char *address, int port, int backlog)
{
    int                sock = -1;
    struct sockaddr_in sa;

    if (Ns_GetSockAddr(&sa, address, port) == NS_OK) {
        Tcl_HashEntry *hPtr;
        Ns_MutexLock(&lock);
        hPtr = Tcl_FindHashEntry(&preboundTcp, (char *) &sa);
        if (hPtr != NULL) {
            sock = (int) Tcl_GetHashValue(hPtr);
            Tcl_DeleteHashEntry(hPtr);
        }
        Ns_MutexUnlock(&lock);
        if (hPtr == NULL) {
            /* Not prebound, bind now */
            sock = Ns_SockBind(&sa);
        }
        if (sock >= 0 && listen(sock, backlog) == -1) {
            /* Can't listen; close the opened socket */
            int err = errno;
            close(sock);
            errno = err;
            sock = -1;
            Ns_SetSockErrno(err);
        }
    }

    /*
     * If forked binder is running and we could not allocate socket
     * directly, try to do it through the binder
     */

    if (sock == -1 && binderRunning) {
        sock = Ns_SockBinderListen('T', address, port, backlog);
    }

    return (SOCKET)sock;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListenUdp --
 *
 *      Listen on the UDP socket for the given IP address and port.
 *
 * Results:
 *      Socket descriptor or -1 on error.
 *
 * Side effects:
 *      May create a new socket if none prebound.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockListenUdp(char *address, int port)
{
    int                sock = -1;
    struct sockaddr_in sa;

    if (Ns_GetSockAddr(&sa, address, port) == NS_OK) {
        Tcl_HashEntry *hPtr;
        Ns_MutexLock(&lock);
        hPtr = Tcl_FindHashEntry(&preboundUdp, (char *) &sa);
        if (hPtr != NULL) {
            sock = (int) Tcl_GetHashValue(hPtr);
            Tcl_DeleteHashEntry(hPtr);
        }
        Ns_MutexUnlock(&lock);
        if (hPtr == NULL) {
            /* Not prebound, bind now */
            sock = Ns_SockBindUdp(&sa);
        }
    }

    /*
     * If forked binder is running and we could not allocate socket
     * directly, try to do it through the binder
     */

    if (sock == -1 && binderRunning) {
        sock = Ns_SockBinderListen('U', address, port, 0);
    }
    return (SOCKET)sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListenRaw --
 *
 *      Listen on the raw socket addressed by the given protocol.
 *
 * Results:
 *      Socket descriptor or -1 on error.
 *
 * Side effects:
 *      May create a new socket if none prebound.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockListenRaw(int proto)
{
    int            sock = -1;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch search;

    Ns_MutexLock(&lock);
    hPtr = Tcl_FirstHashEntry(&preboundRaw, &search);
    while (hPtr != NULL) {
        if (proto == (int)Tcl_GetHashValue(hPtr)) {
            sock = (int)Tcl_GetHashKey(&preboundRaw, hPtr);
            Tcl_DeleteHashEntry(hPtr);
            break;
        }
        hPtr = Tcl_NextHashEntry(&search);
    }
    Ns_MutexUnlock(&lock);
    if (hPtr == NULL) {
        /* Not prebound, bind now */
        sock = Ns_SockBindRaw(proto);
    }

    /*
     * If forked binder is running and we could not allocate socket
     * directly, try to do it through the binder
     */

    if (sock == -1 && binderRunning) {
        sock = Ns_SockBinderListen('R', 0, proto, proto);
    }

    return (SOCKET)sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListenUnix --
 *
 *      Listen on the Unix-domain socket addressed by the given path.
 *
 * Results:
 *      Socket descriptor or -1 on error.
 *
 * Side effects:
 *      May create a new socket if none prebound.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockListenUnix(char *path, int backlog)
{
    int            sock = -1;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch search;

#ifndef _WIN32
    Ns_MutexLock(&lock);
    hPtr = Tcl_FirstHashEntry(&preboundUnix, &search);
    while (hPtr != NULL) {
        if (!strcmp(path, (char*)Tcl_GetHashValue(hPtr))) {
            sock = (int)Tcl_GetHashKey(&preboundRaw, hPtr);
            Tcl_DeleteHashEntry(hPtr);
            break;
        }
        hPtr = Tcl_NextHashEntry(&search);
    }
    Ns_MutexUnlock(&lock);
    if (hPtr == NULL) {
        /* Not prebound, bind now */
        sock = Ns_SockBindUnix(path);
    }
    if (sock >= 0 && listen(sock, backlog) == -1) {
        /* Can't listen; close the opened socket */
        int err = errno;
        close(sock);
        errno = err;
        sock = -1;
        Ns_SetSockErrno(err);
    }

    /*
     * If forked binder is running and we could not allocate socket
     * directly, try to do it through the binder
     */

    if (sock == -1 && binderRunning) {
        sock = Ns_SockBinderListen('D', path, 0, backlog);
    }
#endif

    return (SOCKET) sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockBindUdp --
 *
 *      Create a UDP socket and bind it to the passed-in address.
 *
 * Results:
 *      Socket descriptor or -1 on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockBindUdp(struct sockaddr_in *saPtr)
{
    int sock = -1, n = 1;
   
    sock = socket(AF_INET,SOCK_DGRAM, 0);

    if (sock == -1
        || setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&n,sizeof(n)) == -1
        || bind(sock,(struct sockaddr*)saPtr,sizeof(struct sockaddr_in)) == -1) {
        int err = errno;
        close(sock);
        sock = -1;
        Ns_SetSockErrno(err);
    }

    return (SOCKET)sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockBindUnix --
 *
 *      Create a Unix-domain socket and bind it to the passed-in
 *      file path.
 *
 * Results:
 *      Socket descriptor or -1 on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockBindUnix(char *path)
{
    int                sock = -1;
#ifndef _WIN32    
    struct sockaddr_un addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path,path, sizeof(addr.sun_path) - 1);

    sock = socket(AF_UNIX,SOCK_STREAM, 0);
    
    if (sock == -1
        || bind(sock, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        int err = errno;
        close(sock);
        sock = -1;
        Ns_SetSockErrno(err);
    }
#endif
    
    return (SOCKET) sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockBindRaw --
 *
 *      Create a raw socket. It does not bind, hence the call name
 *      is not entirely correct but is on-pair with other types of
 *      sockets (udp, tcp, unix).
 *
 * Results:
 *      Socket descriptor or -1 on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockBindRaw(int proto)
{
    int sock = -1;
   
    sock = socket(AF_INET,SOCK_RAW, proto);

    if (sock == -1) {
        int err = errno;
        close(sock);
        Ns_SetSockErrno(err);
    }

    return (SOCKET)sock;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitBinder --
 *
 *      Initialize the pre-bind tables.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
NsInitBinder(void)
{
    Tcl_InitHashTable(&preboundTcp, sizeof(struct sockaddr_in)/sizeof(int));
    Tcl_InitHashTable(&preboundUdp, sizeof(struct sockaddr_in)/sizeof(int));
    Tcl_InitHashTable(&preboundRaw, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable(&preboundUnix, TCL_STRING_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * NsPreBind --
 *
 *      Pre-bind any requested ports (called from Ns_Main at startup).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May pre-bind to one or more ports.
 *
 *----------------------------------------------------------------------
 */

void
NsPreBind(char *args, char *file)
{
#ifndef _WIN32
    if (args != NULL) {
        PreBind(args);
    }
    if (file != NULL) {
        Tcl_Channel chan = Tcl_OpenFileChannel(NULL, file, "r", 0);
        if (chan == NULL) {
            Ns_Log(Error, "NsPreBind: can't open file '%s': '%s'", file,
                   strerror(Tcl_GetErrno()));
        } else {
            Tcl_DString line;
            Tcl_DStringInit(&line);
            while(!Tcl_Eof(chan)) {
                Tcl_DStringSetLength(&line, 0);
                if (Tcl_Gets(chan, &line) > 0) {
                    PreBind(Tcl_DStringValue(&line));
                }
            }
            Tcl_DStringFree(&line);
            Tcl_Close(NULL, chan);
        }
    }
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * NsClosePreBound --
 *
 *      Close remaining pre-bound sockets not consumed by anybody.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Pre-bind hash-tables are cleaned and re-initialized.
 *
 *----------------------------------------------------------------------
 */

void
NsClosePreBound(void)
{
#ifdef _WIN32
    return;
#else
    Tcl_HashEntry      *hPtr;
    Tcl_HashSearch     search;
    char               *addr;
    int                port, sock;
    struct sockaddr_in *saPtr;
    
    Ns_MutexLock(&lock);

    /*
     * Close TCP sockets
     */

    hPtr = Tcl_FirstHashEntry(&preboundTcp, &search);
    while (hPtr != NULL) {
        saPtr = (struct sockaddr_in *) Tcl_GetHashKey(&preboundTcp, hPtr);
        addr = ns_inet_ntoa(saPtr->sin_addr);
        port = htons(saPtr->sin_port);
        sock = (int)Tcl_GetHashValue(hPtr);
        Ns_Log(Warning, "prebind: closed unused TCP socket: %s:%d = %d", 
               addr, port, sock);
        close(sock);
        Tcl_DeleteHashEntry(hPtr);
        hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&preboundTcp);
    Tcl_InitHashTable(&preboundTcp, sizeof(struct sockaddr_in)/sizeof(int));

    /*
     * Close UDP sockets
     */

    hPtr = Tcl_FirstHashEntry(&preboundUdp, &search);
    while (hPtr != NULL) {
        saPtr = (struct sockaddr_in *) Tcl_GetHashKey(&preboundUdp, hPtr);
        addr = ns_inet_ntoa(saPtr->sin_addr);
        port = htons(saPtr->sin_port);
        sock = (int)Tcl_GetHashValue(hPtr);
        Ns_Log(Warning, "prebind: closed unused UDP socket: %s:%d = %d", 
               addr, port, sock);
        close(sock);
        Tcl_DeleteHashEntry(hPtr);
        hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&preboundUdp);
    Tcl_InitHashTable(&preboundUdp, sizeof(struct sockaddr_in)/sizeof(int));

    /*
     * Close raw sockets
     */

    hPtr = Tcl_FirstHashEntry(&preboundRaw, &search);
    while (hPtr != NULL) {
        sock = (int)Tcl_GetHashKey(&preboundRaw, hPtr);
        port = (int)Tcl_GetHashValue(hPtr);
        Ns_Log(Warning, "prebind: closed unused raw socket: %d = %d", 
               port, sock);
        close(sock);
        Tcl_DeleteHashEntry(hPtr);
        hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&preboundRaw);
    Tcl_InitHashTable(&preboundRaw, TCL_ONE_WORD_KEYS);

    /*
     * Close Unix-domain sockets
     */

    hPtr = Tcl_FirstHashEntry(&preboundUnix, &search);
    while (hPtr != NULL) {
        addr = (char *) Tcl_GetHashKey(&preboundUnix, hPtr);
        sock = (int)Tcl_GetHashValue(hPtr);
        Ns_Log(Warning, "prebind: closed unused Unix-domain socket: %s = %d",
               addr, sock);
        close(sock);
        Tcl_DeleteHashEntry(hPtr);
        hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&preboundUnix);
    Tcl_InitHashTable(&preboundUnix, TCL_STRING_KEYS);

    Ns_MutexUnlock(&lock);
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * PreBind --
 *
 *      Pre-bind to one or more ports in a comma-separated list:
 *
 *          addr:port[/protocol]
 *          port[/protocol]
 *          0/icmp[/count]
 *          /path
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sockets are left in bound state for later listen 
 *      in Ns_SockListenXXX.  
 *
 *----------------------------------------------------------------------
 */

static void
PreBind(char *line)
{
#ifdef _WIN32
    return;
#else
    Tcl_HashEntry      *hPtr;
    int                new, sock, port;
    char               *next, *str, *addr, *proto;
    struct sockaddr_in sa;

    for (;line != NULL; line = next) {
        next = strchr(line, ',');
        if (next) {
            *next++ = '\0';
        }
        proto = "tcp";
        addr = "0.0.0.0";
        /* Parse port */
        str = strchr(line, ':');
        if (str) {
            *str++ = '\0';
            port = atoi(str);
            addr = line;
            line = str;
        } else {
            port = atoi(line);
        }
        /* Parse protocol */
        if (*line != '/' && (str = strchr(line,'/'))) {
            *str++ = '\0';
            proto = str;
        }
        
        if (!strcmp(proto,"tcp") && port > 0) {
            if (Ns_GetSockAddr(&sa, addr, port) != NS_OK) {
                Ns_Log(Error, "prebind: tcp: invalid address: %s:%d",
                       addr, port);
                continue;
            }
            hPtr = Tcl_CreateHashEntry(&preboundTcp, (char *) &sa, &new);
            if (!new) {
                Ns_Log(Error, "prebind: tcp: duplicate entry: %s:%d",
                       addr, port);
                continue;
            }
            sock = Ns_SockBind(&sa);
            if (sock == -1) {
                Ns_Log(Error, "prebind: tcp: %s:%d: %s", addr, port,
                       strerror(errno));
                Tcl_DeleteHashEntry(hPtr);
                continue;
            }
            Tcl_SetHashValue(hPtr, sock);
            Ns_Log(Notice, "prebind: tcp: %s:%d = %d", addr, port, sock);
        }

        if (!strcmp(proto,"udp") && port > 0) {
            if (Ns_GetSockAddr(&sa, addr, port) != NS_OK) {
                Ns_Log(Error, "prebind: udp: invalid address: %s:%d",
                       addr, port);
                continue;
            }
            hPtr = Tcl_CreateHashEntry(&preboundUdp, (char *) &sa, &new);
            if (!new) {
                Ns_Log(Error, "prebind: udp: duplicate entry: %s:%d",
                       addr, port);
                continue;
            }
            sock = Ns_SockBindUdp(&sa);
            if (sock == -1) {
                Ns_Log(Error, "prebind: udp: %s:%d: %s", addr, port,
                       strerror(errno));
                Tcl_DeleteHashEntry(hPtr);
                continue;
            }
            Tcl_SetHashValue(hPtr, sock);
            Ns_Log(Notice, "prebind: udp: %s:%d = %d", addr, port, sock);
        }

        if (!strncmp(proto,"icmp",4)) {
            int count = 1;
            /* Parse count */
            str = strchr(str,'/');
            if (str) {
                *(str++) = '\0';
                count = atoi(str);
            }
            while(count--) {
                sock = Ns_SockBindRaw(IPPROTO_ICMP);
                if (sock == -1) {
                    Ns_Log(Error, "prebind: icmp: %s",strerror(errno));
                    continue;
                }
                hPtr = Tcl_CreateHashEntry(&preboundRaw, (char *) sock, &new);
                if (!new) {
                    Ns_Log(Error, "prebind: icmp: duplicate entry");
                    close(sock);
                    continue;
                }
                Tcl_SetHashValue(hPtr, IPPROTO_ICMP);
                Ns_Log(Notice, "prebind: icmp: %d", sock);
            }
        }

        if (Ns_PathIsAbsolute(line)) {
            hPtr = Tcl_CreateHashEntry(&preboundUnix, (char *) line, &new);
            if (!new) {
                Ns_Log(Error, "prebind: unix: duplicate entry: %s",line);
                continue;
            }
            sock = Ns_SockBindUnix(line);
            if (sock == -1) {
                Ns_Log(Error, "prebind: unix: %s: %s", proto, strerror(errno));
                Tcl_DeleteHashEntry(hPtr);
                continue;
            }
            Tcl_SetHashValue(hPtr, sock);
            Ns_Log(Notice, "prebind: unix: %s = %d", line, sock);
        }
    }
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockBinderListen --
 *
 *      Create a new TCP/UDP/Unix socket bound to the specified port
 *      and listening for new connections.
 *
 *      The following types are defined:
 *      T - TCP socket
 *      U - UDP socket
 *      D - Unix domain socket
 *      R - raw socket
 *
 * Results:
 *      Socket descriptor or -1 on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockBinderListen(int type, char *address, int port, int options)
{
    int           err;
    SOCKET        sock = -1;
    char          data[64];
#ifndef WIN32
    struct msghdr msg;
    struct iovec  iov[4];

#ifdef HAVE_CMMSG
    CMsg cm;
#endif

    if (address == NULL) {
        address = "0.0.0.0";
    }
    iov[0].iov_base = (caddr_t) &options;
    iov[0].iov_len = sizeof(options);
    iov[1].iov_base = (caddr_t) &port;
    iov[1].iov_len = sizeof(port);
    iov[2].iov_base = (caddr_t) &type;
    iov[2].iov_len = sizeof(type);
    iov[3].iov_base = (caddr_t) data;
    iov[3].iov_len = sizeof(data);
    memset(data, 0, sizeof(data));
    strncpy(data, address, sizeof(data)-1);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 4;
    if (sendmsg(binderRequest[1], (struct msghdr *) &msg, 0) != REQUEST_SIZE) {
        Ns_Log(Error, "Ns_SockBinderListen: sendmsg() failed: '%s'",
               strerror(errno));
        return -1;
    }

    iov[0].iov_base = (caddr_t) &err;
    iov[0].iov_len = sizeof(int);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
#ifdef HAVE_CMMSG
    cm.len = sizeof(cm);
    cm.type = SCM_RIGHTS;
    cm.level = SOL_SOCKET;
    msg.msg_control = (void *) &cm;
    msg.msg_controllen = cm.len;
    msg.msg_flags = 0;
#else
    msg.msg_accrights = (caddr_t) &sock;
    msg.msg_accrightslen = sizeof(sock);
#endif
    if (recvmsg(binderResponse[0], (struct msghdr *) &msg, 0) != RESPONSE_SIZE) {
        Ns_Log(Error, "Ns_SockBinderListen: recvmsg() failed: '%s'", 
               strerror(errno));
        return -1;
    }

#ifdef HAVE_CMMSG
    sock = cm.fds[0];
#endif

    /*
     * Close-on-exec, while set in the binder process by default
     * with Ns_SockBind, is not transmitted in the sendmsg and
     * must be set again.
     */

    if (sock != INVALID_SOCKET && Ns_CloseOnExec(sock) != NS_OK) {
        close(sock);
        sock = -1;
    }
    if (err == 0) {
        Ns_Log(Notice, "Ns_SockBinderListen: listen(%s,%d) = %d",
               address, port, sock);
    } else {
        Ns_SetSockErrno(err);
        sock = -1;
        Ns_Log(Error, "Ns_SockBinderListen: listen(%s,%d) failed: '%s'",
               address, port, ns_sockstrerror(ns_sockerrno));
    }
#endif /* _WIN32 */
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * NsForkBinder --
 *
 *      Fork of the slave bind/listen process.  This routine is called
 *      by main() when the server starts as root.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The binderRunning, binderRequest, binderResponse static variables
 *      are updated.
 *
 *----------------------------------------------------------------------
 */

void
NsForkBinder(void)
{
    int pid, status;
#ifndef _WIN32
    /*
     * Create two socket pipes, one for sending the request and one
     * for receiving the response.
     */

    if (ns_sockpair(binderRequest) != 0 || ns_sockpair(binderResponse) != 0) {
        Ns_Fatal("NsForkBinder: ns_sockpair() failed: '%s'", strerror(errno));
    }

    /*
     * Double-fork and run as a binder until the socket pairs are
     * closed.  The server double forks to avoid problems 
     * waiting for a child root process after the parent does a
     * setuid(), something which appears to confuse the
     * process-based Linux and SGI threads.
     */

    pid = ns_fork();
    if (pid < 0) {
        Ns_Fatal("NsForkBinder: fork() failed: '%s'", strerror(errno));
    } else if (pid == 0) {
        pid = ns_fork();
        if (pid < 0) {
            Ns_Fatal("NsForkBinder: fork() failed: '%s'", strerror(errno));
        } else if (pid == 0) {
            close(binderRequest[1]);
            close(binderResponse[0]);
            Binder();
        }
        exit(0);
    }
    if (Ns_WaitForProcess(pid, &status) != NS_OK) {
        Ns_Fatal("NsForkBinder: Ns_WaitForProcess(%d) failed: '%s'",
                 pid, strerror(errno));
    } else if (status != 0) {
        Ns_Fatal("NsForkBinder: process %d exited with non-zero status: %d",
                 pid, status);
    }
#endif /* _WIN32 */
    binderRunning = 1;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStopBinder --
 *
 *      Close the socket to the binder after startup.  This is done
 *      to avoid a possible security risk of binding to privileged
 *      ports after startup.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Binder process will exit.
 *
 *----------------------------------------------------------------------
 */

void
NsStopBinder(void)
{
    if (binderRunning) {
        close(binderRequest[1]);
        close(binderResponse[0]);
        close(binderRequest[0]);
        close(binderResponse[1]);
        binderRunning = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Binder --
 *
 *      Slave process bind/listen loop.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sockets are created and sent to the parent on request.
 *
 *----------------------------------------------------------------------
 */

static void
Binder(void)
{
    int           options, type, port, n, err, fd;
    char          address[64];
#ifndef _WIN32
    struct msghdr msg;
    struct iovec  iov[4];

#ifdef HAVE_CMMSG
    CMsg cm;
#endif

    Ns_Log(Notice, "binder: started");

    /*
     * Endlessly listen for socket bind requests.
     */

    for (;;) {
        iov[0].iov_base = (caddr_t) &options;
        iov[0].iov_len = sizeof(options);
        iov[1].iov_base = (caddr_t) &port;
        iov[1].iov_len = sizeof(port);
        iov[2].iov_base = (caddr_t) &type;
        iov[2].iov_len = sizeof(type);
        iov[3].iov_base = (caddr_t) address;
        iov[3].iov_len = sizeof(address);
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov;
        msg.msg_iovlen = 4;
        type = 0;
        err = 0;
        do {
            n = recvmsg(binderRequest[0], (struct msghdr *) &msg, 0);
        } while (n == -1 && errno == EINTR);
        if (n == 0) {
            break;
        }
        if (n != REQUEST_SIZE) {
            Ns_Fatal("binder: recvmsg() failed: '%s'", strerror(errno));
        }

        /*
         * NB: Due to a bug in Solaris the slave process must
         * call both bind() and listen() before returning the
         * socket.  All other Unix versions would actually allow
         * just performing the bind() in the slave and allowing
         * the parent to perform the listen().
         */
        switch (type) {
        case 'U':
            fd = Ns_SockListenUdp(address, port);
            break;
        case 'D':
            fd = Ns_SockListenUnix(address, options);
            break;
        case 'R':
            fd = Ns_SockListenRaw(options);
            break;
        case 'T':
        default:
            fd = Ns_SockListenEx(address, port, options);
        }

        if (fd < 0) {
            err = errno;
        }

        iov[0].iov_base = (caddr_t) &err;
        iov[0].iov_len = sizeof(err);
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        if (fd != -1) {
#ifdef HAVE_CMMSG
            cm.len = sizeof(cm);
            cm.level = SOL_SOCKET;
            cm.type = SCM_RIGHTS;
            cm.fds[0] = fd;
            msg.msg_control = (void *) &cm;
            msg.msg_controllen = cm.len;
            msg.msg_flags = 0;
#else
            msg.msg_accrights = (caddr_t) &fd;
            msg.msg_accrightslen = sizeof(fd);
#endif
        }
        do {
            n = sendmsg(binderResponse[1], (struct msghdr *) &msg, 0);
        } while (n == -1 && errno == EINTR);
        if (n != RESPONSE_SIZE) {
            Ns_Fatal("binder: sendmsg() failed: '%s'", strerror(errno));
        }
        if (fd != -1) {
    
            /*
             * Close the socket as it won't be needed in the slave.
             */

            close(fd);
        }
    }
#endif /* _WIN32 */
    Ns_Log(Notice, "binder: stopped");
}
