/*
 * net_xbox.c -- networking for the Xbox build: loopback only.
 *
 * Replaces qcommon/net_ip.c. Single player runs the client and the server in
 * the same process, talking through the loopback queues in net_chan.c, so no
 * socket is opened here. The address comparison functions are the portable
 * ones from net_ip.c.
 *
 * TODO LAN multiplayer: open a UDP socket with lwIP (nxdk/lib/net).
 */
#include "q_shared.h"
#include "qcommon.h"

static cvar_t *net_enabled;

char *NET_ErrorString(void)
{
    return "no network on Xbox build";
}

qboolean Sys_StringToAdr(const char *s, netadr_t *a, netadrtype_t family)
{
    (void)family;
    if (!Q_stricmp(s, "localhost") || !Q_stricmp(s, "loopback")) {
        memset(a, 0, sizeof(*a));
        a->type = NA_LOOPBACK;
        return qtrue;
    }
    return qfalse;
}

qboolean NET_CompareBaseAdrMask(netadr_t a, netadr_t b, int netmask)
{
    byte cmpmask, *addra, *addrb;
    int  curbyte;

    if (a.type != b.type)
        return qfalse;
    if (a.type == NA_LOOPBACK || a.type == NA_BOT)
        return qtrue;

    if (a.type == NA_IP) {
        addra = (byte *)&a.ip;
        addrb = (byte *)&b.ip;
        if (netmask < 0 || netmask > 32)
            netmask = 32;
    } else if (a.type == NA_IP6) {
        addra = (byte *)&a.ip6;
        addrb = (byte *)&b.ip6;
        if (netmask < 0 || netmask > 128)
            netmask = 128;
    } else {
        Com_Printf("NET_CompareBaseAdr: bad address type\n");
        return qfalse;
    }

    curbyte = netmask >> 3;
    if (curbyte && memcmp(addra, addrb, curbyte))
        return qfalse;

    netmask &= 0x07;
    if (!netmask)
        return qtrue;

    cmpmask = (1 << netmask) - 1;
    cmpmask <<= 8 - netmask;
    return (addra[curbyte] & cmpmask) == (addrb[curbyte] & cmpmask);
}

qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    return NET_CompareBaseAdrMask(a, b, -1);
}

const char *NET_AdrToString(netadr_t a)
{
    static char s[NET_ADDRSTRMAXLEN];

    if (a.type == NA_LOOPBACK)
        Com_sprintf(s, sizeof(s), "loopback");
    else if (a.type == NA_BOT)
        Com_sprintf(s, sizeof(s), "bot");
    else if (a.type == NA_IP)
        Com_sprintf(s, sizeof(s), "%u.%u.%u.%u", a.ip[0], a.ip[1], a.ip[2], a.ip[3]);
    else
        Com_sprintf(s, sizeof(s), "unknown");
    return s;
}

const char *NET_AdrToStringwPort(netadr_t a)
{
    static char s[NET_ADDRSTRMAXLEN];

    if (a.type == NA_IP)
        Com_sprintf(s, sizeof(s), "%s:%hu", NET_AdrToString(a), BigShort(a.port));
    else
        Q_strncpyz(s, NET_AdrToString(a), sizeof(s));
    return s;
}

qboolean NET_CompareAdr(netadr_t a, netadr_t b)
{
    if (!NET_CompareBaseAdr(a, b))
        return qfalse;
    if (a.type == NA_IP || a.type == NA_IP6)
        return a.port == b.port;
    return qtrue;
}

qboolean NET_IsLocalAddress(netadr_t adr)
{
    return adr.type == NA_LOOPBACK;
}

void Sys_SendPacket(int length, const void *data, netadr_t to)
{
    (void)length;
    (void)data;
    (void)to;
}

qboolean Sys_IsLANAddress(netadr_t adr)
{
    return adr.type == NA_LOOPBACK;
}

void Sys_ShowIP(void)
{
    Com_Printf("IP: loopback only\n");
}

void NET_JoinMulticast6(void) {}
void NET_LeaveMulticast6(void) {}

void NET_Config(qboolean enableNetworking)
{
    (void)enableNetworking;
}

void NET_Restart_f(void) {}

void NET_Init(void)
{
    net_enabled = Cvar_Get("net_enabled", "0", CVAR_LATCH | CVAR_ARCHIVE);
    Cmd_AddCommand("net_restart", NET_Restart_f);
}

void NET_Shutdown(void) {}

void NET_Sleep(int msec)
{
    if (msec > 0)
        Sys_Sleep(msec);
}
