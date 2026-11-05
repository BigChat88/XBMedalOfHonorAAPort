// winsock.h for nxdk: the Winsock API on top of lwIP's BSD sockets.
// Single player does not use the network; this lets GameSpy compile
// untouched and leaves the door open to LAN multiplayer.
#pragma once

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/errno.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)

#define closesocket  lwip_close
#define ioctlsocket  lwip_ioctl

#define WSAGetLastError() (errno)
#define WSASetLastError(e) (errno = (e))
#define WSAEWOULDBLOCK   EWOULDBLOCK
#define WSAECONNRESET    ECONNRESET
#define WSAECONNREFUSED  ECONNREFUSED
#define WSAEMSGSIZE      EMSGSIZE
#define WSAEINPROGRESS   EINPROGRESS
#define WSAEALREADY      EALREADY
#define WSAEISCONN       EISCONN
#define WSAENOTCONN      ENOTCONN
#define WSAEADDRNOTAVAIL EADDRNOTAVAIL
#define WSAEAFNOSUPPORT  EAFNOSUPPORT
#define WSAENETDOWN      ENETDOWN
#define WSAENETUNREACH   ENETUNREACH
#define WSAEHOSTUNREACH  EHOSTUNREACH
#define WSAETIMEDOUT     ETIMEDOUT
#define WSAEINVAL        EINVAL
#define WSAEINTR         EINTR
#define WSANOTINITIALISED 10093

typedef struct WSAData {
    unsigned short wVersion;
    unsigned short wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
} WSADATA, *LPWSADATA;

static inline int WSAStartup(unsigned short v, LPWSADATA d) { (void)v; (void)d; return 0; }
static inline int WSACleanup(void) { return 0; }
#ifndef MAKEWORD
#define MAKEWORD(a, b) ((unsigned short)(((unsigned char)(a)) | ((unsigned short)((unsigned char)(b))) << 8))
#endif

typedef struct hostent HOSTENT, *PHOSTENT, *LPHOSTENT;
typedef struct sockaddr SOCKADDR, *PSOCKADDR, *LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;
typedef struct in_addr IN_ADDR, *PIN_ADDR;
typedef struct timeval TIMEVAL;
typedef unsigned long u_long;
typedef unsigned short u_short;
typedef unsigned char u_char;
typedef unsigned int u_int;
