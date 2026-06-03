/*
 * tcpip.h - minimal Marinetti (TCP/IP tool set $36) interface for ORCA/C.
 *
 * Hand-built from the canonical Marinetti assembly definitions that ship with
 * ORCA / Golden Gate, so we have NO external-header dependency:
 *   - dispatch numbers + parameter order: TCPIP.MACS.S
 *   - record layouts + constants:         E16.TCPIP (Marinetti equates)
 *
 * ORCA/C calls a tool inline: inline(0xCCTT, dispatcher) does
 *   LDX #$CCTT ; JSL $E10000   (CC = call number, TT = tool set = $36).
 * The function's return TYPE tells ORCA/C how much result space to reserve
 * (1 word for Word, 2 for LongWord, none for void) - matching the PHA/PHS in
 * the macros. After a call, tool error is in _toolErr (see orca.h toolerror()).
 *
 * Only the calls the miner/echo client needs are declared here.
 */
#ifndef __TCPIP_H__
#define __TCPIP_H__

#ifndef __TYPES__
#include <types.h>
#endif

/* ---- TCP connection states (srState) ---- */
#define TCPSCLOSED       0
#define TCPSLISTEN       1
#define TCPSSYNSENT      2
#define TCPSSYNRCVD      3
#define TCPSESTABLISHED  4
#define TCPSFINWAIT1     5
#define TCPSFINWAIT2     6
#define TCPSCLOSEWAIT    7
#define TCPSLASTACK      8
#define TCPSCLOSING      9
#define TCPSTIMEWAIT     10

/* ---- TCPIPReadTCP response buffer (rrlen = 14 bytes) ---- */
typedef struct rrBuff {
    LongWord rrBuffCount;     /* @0  bytes actually transferred         */
    Handle   rrBuffHandle;    /* @4  handle (when buffType requests one) */
    Word     rrMoreFlag;      /* @8  more data is waiting                */
    Word     rrPushFlag;      /* @10 PUSH seen                           */
    Word     rrUrgentFlag;    /* @12 urgent data                         */
} rrBuff;

/* ---- TCPIPStatusTCP response buffer (srlen = 22 bytes) ---- */
typedef struct srBuff {
    Word     srState;         /* @0  TCPS* connection state              */
    Word     srNetworkError;  /* @2                                      */
    LongWord srSndQueued;     /* @4  bytes queued to send                */
    LongWord srRcvQueued;     /* @8  bytes available to read             */
    LongWord srDestIP;        /* @12 remote IP                           */
    Word     srDestPort;      /* @16 remote port                         */
    Word     srConnectType;   /* @18                                     */
    Word     srAcceptCount;   /* @20                                     */
} srBuff;

/* buffType values for TCPIPReadTCP */
#define rrBuffTypePointer 0x0000   /* read into the caller's pointer */
#define rrBuffTypeNewHandle 0x0001 /* Marinetti allocates a handle   */

/* ---- DNR (domain name resolver) status codes (E16.TCPIP equates) ---- */
#define DNR_Pending      0   /* request still being processed        */
#define DNR_OK           1   /* resolved: DNRIPaddress is valid       */
#define DNR_Failed       2   /* network error / timeout               */
#define DNR_NoDNSEntry   3   /* domain has no DNS entry               */
#define DNR_Cancelled    4   /* cancelled via TCPIPCancelDNR          */

/* DNR result record. Marinetti writes DNRstatus, and (Name->IP) the resolved
 * DNRIPaddress at offset 2 - which OVERLAYS the canonical-name area the resolver
 * may also write there, so DNRname[] reserves room past the IP. (E16.TCPIP:
 * DNRstatus@0, DNRname@2, DNRIPaddress@2.) Pass a POINTER to one of these. */
typedef struct dnrBuff {
    Word     DNRstatus;       /* @0  DNR_*                              */
    LongWord DNRIPaddress;    /* @2  resolved IP, Marinetti byte order  */
    char     DNRname[256];    /* @6  reserve: name overlay begins at @2 */
} dnrBuff;

/* ---- the calls ---- (call numbers are the high byte; tool set = $36) */
extern pascal void     TCPIPStartUp(void)               inline(0x0236,dispatcher);
extern pascal Boolean  TCPIPGetConnectStatus(void)      inline(0x0936,dispatcher);
extern pascal LongWord TCPIPGetMyIPAddress(void)        inline(0x0F36,dispatcher);
extern pascal void     TCPIPPoll(void)                  inline(0x2236,dispatcher);
extern pascal Word     TCPIPLogin(Word userID, LongWord destIP,
                                  Word destPort, Word tos, Word ttl)
                                                        inline(0x2336,dispatcher);
extern pascal void     TCPIPLogout(Word ipid)           inline(0x2436,dispatcher);
extern pascal Word     TCPIPOpenTCP(Word ipid)          inline(0x2C36,dispatcher);
extern pascal Word     TCPIPWriteTCP(Word ipid, Ref buffStart,
                                     LongWord buffLength, Word push, Word async)
                                                        inline(0x2D36,dispatcher);
extern pascal Word     TCPIPReadTCP(Word ipid, Word buffType,
                                    Ref buffHandleOrPtr, LongWord requestCount,
                                    Ref rrBuffPtr)
                                                        inline(0x2E36,dispatcher);
extern pascal Word     TCPIPCloseTCP(Word ipid)         inline(0x2F36,dispatcher);
extern pascal Word     TCPIPStatusTCP(Word ipid, Ref srBuffPtr)
                                                        inline(0x3136,dispatcher);

/* DNR: cnamePtr -> a PASCAL string (length byte + chars). dnrBuffPtr -> dnrBuff.
 * Async: kicks off the lookup, then poll dnrBuff.DNRstatus (calling TCPIPPoll)
 * until it leaves DNR_Pending. TCPIPCancelDNR aborts a pending lookup. */
extern pascal void     TCPIPDNRNameToIP(Ref cnamePtr, Ref dnrBuffPtr)
                                                        inline(0x2136,dispatcher);
extern pascal void     TCPIPCancelDNR(Ref dnrBuffPtr)   inline(0x2036,dispatcher);

#endif /* __TCPIP_H__ */
