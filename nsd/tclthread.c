/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
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
 * tclthread.c --
 *
 *      Tcl wrappers around all thread objects
 */

#include "nsd.h"

NS_RCSID("@(#) $Header$");


/*
 * The following structure defines the script to run
 * in a Tcl thread.
 */

typedef struct TclThreadArg {
    char *server;
    int   detached;
    char  script[1];
} TclThreadArg;

/*
 * Local functions defined in this file
 */

static int GetArgs(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[],
                   CONST char *opts[], int *optPtr, int createOpt, int destroyOpt,
                   CONST char *type, void **addrPtr, Tcl_HashTable *table);
static int GetAddr(Tcl_Interp *interp, Tcl_Obj *argObj, CONST char *type,
                   void **addrPtr, Tcl_HashTable *table);
static void CreateTclThread(NsInterp *itPtr, char *script, int detached,
                            Ns_Thread *thrPtr);

/*
 * Local variables defined in this file.
 */

static CONST char *mutexAddr   = "ns:mutex";
static CONST char *critsecAddr = "ns:critsec";
static CONST char *semaAddr    = "ns:semaphore";
static CONST char *condAddr    = "ns:condition";
static CONST char *rwlockAddr  = "ns:rwlock";
static CONST char *threadAddr  = "ns:thread";


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclThread --
 *
 *      Run a Tcl script in a new thread and wait for the result.
 *
 * Results:
 *      NS_OK.  String result of script available via thrPtr.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclThread(Tcl_Interp *interp, char *script, Ns_Thread *thrPtr)
{
    NsInterp *itPtr = NsGetInterpData(interp);

    CreateTclThread(itPtr, script, (thrPtr == NULL), thrPtr);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDetachedThread --
 *
 *      Run a Tcl script in a detached thread, returning immediately.
 *
 * Results:
 *      NS_OK.
 *
 * Side effects:
 *      Result of script is discarded.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclDetachedThread(Tcl_Interp *interp, char *script)
{
    return Ns_TclThread(interp, script, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclThreadObjCmd --
 *
 *      Implements ns_thread to get data on the current thread and
 *      create and wait on new Tcl-script based threads.  New threads will
 *      be created in the virtual-server context of the current interp,
 *      if any.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      May create a new thread or wait for an existing thread to exit.
 *
 *----------------------------------------------------------------------
 */

int
NsTclThreadObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp  *itPtr = arg;
    void      *tidArg;
    Ns_Thread  tid;
    void      *result;
    char      *script;
    int        opt;

    static CONST char *opts[] = {
        "begin", "begindetached", "create", "wait", "join",
        "name", "get", "getid", "id", "yield", NULL
    };
    enum {
        TBeginIdx, TBeginDetachedIdx, TCreateIdx, TWaitIdx, TJoinIdx,
        TNameIdx, TGetIdx, TGetIdIdx, TIdIdx, TYieldIdx
    };

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0, &opt)
        != TCL_OK) {
        return TCL_ERROR;
    }

    switch (opt) {
    case TBeginIdx:
    case TBeginDetachedIdx:
    case TCreateIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "script");
            return TCL_ERROR;
        }
        script = Tcl_GetString(objv[2]);
        if (opt == TBeginDetachedIdx) {
            CreateTclThread(itPtr, script, 1, NULL);
        } else {
            CreateTclThread(itPtr, script, 0, &tid);
            Ns_TclSetAddrObj(Tcl_GetObjResult(interp), threadAddr, tid);
        }
        break;

    case TWaitIdx:
    case TJoinIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tid");
            return TCL_ERROR;
        }
        if (Ns_TclGetAddrFromObj(interp, objv[2], threadAddr, &tidArg)
            != TCL_OK) {
            return TCL_ERROR;
        }
        tid = tidArg;
        Ns_ThreadJoin(&tid, &result);
        Tcl_SetResult(interp, (char *) result, (Tcl_FreeProc *) ns_free);
        break;

    case TGetIdx:
        Ns_ThreadSelf(&tid);
        Ns_TclSetAddrObj(Tcl_GetObjResult(interp), threadAddr, tid);
        break;

    case TIdIdx:
    case TGetIdIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_ThreadId()));
        break;

    case TNameIdx:
        if (objc > 2) {
            Ns_ThreadSetName(Tcl_GetString(objv[2]));
        }
        Tcl_SetResult(interp, Ns_ThreadGetName(), TCL_VOLATILE);
        break;

    case TYieldIdx:
        Ns_ThreadYield();
        break;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclMutexObjCmd --
 *
 *      Implements ns_mutex.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclMutexObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = (NsInterp *) arg;
    void     *lockArg;
    Ns_Mutex *lockPtr;
    int       opt, status = TCL_OK;

    static CONST char *opts[] = {
        "create", "destroy", "eval", "lock", "trylock", "unlock", NULL
    };
    enum {
        MCreateIdx, MDestroyIdx, MEvalIdx, MLockIdx, MTryLockIdx, MUnlockIdx
    };
    if (GetArgs(interp, objc, objv, opts, &opt, MCreateIdx, MDestroyIdx,
                mutexAddr, &lockArg, &itPtr->servPtr->tcl.mutexTable) != TCL_OK) {
        return TCL_ERROR;
    }
    lockPtr = (Ns_Mutex*) lockArg;

    switch (opt) {
    case MCreateIdx:
        Ns_MutexInit(lockPtr);
        if (objc > 2) {
            Ns_MutexSetName(lockPtr, Tcl_GetString(objv[2]));
        }
        break;
    case MLockIdx:
        Ns_MutexLock(lockPtr);
        break;
    case MTryLockIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_MutexTryLock(lockPtr)));
        break;
    case MUnlockIdx:
        Ns_MutexUnlock(lockPtr);
        break;
    case MEvalIdx:
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "script");
            return TCL_ERROR;
        }
        Ns_MutexLock(lockPtr);
        status = Tcl_EvalObjEx(interp, objv[3], 0);
        Ns_MutexUnlock(lockPtr);
        break;
    case MDestroyIdx:
        Ns_MutexDestroy(lockPtr);
        ns_free(lockPtr);
        break;
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCritSecObjCmd --
 *
 *      Implements ns_critsec.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See doc.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCritSecObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = (NsInterp *) arg;
    void     *csArg;
    Ns_Cs    *csPtr;
    int       opt, status = TCL_OK;

    static CONST char *opts[] = {
        "create", "destroy", "enter", "eval", "leave", NULL
    };
    enum {
        CCreateIdx, CDestroyIdx, CEnterIdx, CEvalIdx, CLeaveIdx
    };
    if (GetArgs(interp, objc, objv, opts, &opt, CCreateIdx, CDestroyIdx,
                critsecAddr, &csArg, &itPtr->servPtr->tcl.csTable) != TCL_OK) {
        return TCL_ERROR;
    }
    csPtr = (Ns_Cs*) csArg;

    switch (opt) {
    case CCreateIdx:
        Ns_CsInit(csPtr);
        break;
    case CEnterIdx:
        Ns_CsEnter(csPtr);
        break;
    case CLeaveIdx:
        Ns_CsLeave(csPtr);
        break;
    case CEvalIdx:
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "script");
            return TCL_ERROR;
        }
        Ns_CsEnter(csPtr);
        status = Tcl_EvalObjEx(interp, objv[3], 0);
        Ns_CsLeave(csPtr);
        break;
    case CDestroyIdx:
        Ns_CsDestroy(csPtr);
        ns_free(csPtr);
        break;
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSemaObjCmd --
 *
 *      Implements ns_sema.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclSemaObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = (NsInterp *) arg;
    void     *semaArg;
    Ns_Sema  *semaPtr;
    int       opt, cnt;

    static CONST char *opts[] = {
        "create", "destroy", "release", "wait", NULL
    };
    enum {
        SCreateIdx, SDestroyIdx, SReleaseIdx, SWaitIdx
    };
    if (GetArgs(interp, objc, objv, opts, &opt, SCreateIdx, SDestroyIdx,
                semaAddr, &semaArg, &itPtr->servPtr->tcl.semaTable) != TCL_OK) {
        return TCL_ERROR;
    }
    semaPtr = (Ns_Sema*) semaArg;

    switch (opt) {
    case SCreateIdx:
        if (objc < 3) {
            cnt = 0;
        } else if (Tcl_GetIntFromObj(interp, objv[2], &cnt) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_SemaInit(semaPtr, cnt);
        break;

    case SReleaseIdx:
        if (objc < 4) {
            cnt = 1;
        } else if (Tcl_GetIntFromObj(interp, objv[3], &cnt) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_SemaPost(semaPtr, cnt);
        break;

    case SWaitIdx:
        Ns_SemaWait(semaPtr);
        break;

    case SDestroyIdx:
        Ns_SemaDestroy(semaPtr);
        ns_free(semaPtr);
        break;
    }

    return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * NsTclCondObjCmd --
 *
 *      Implements ns_cond.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCondObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = (NsInterp *) arg;
    void     *condArg;
    void     *lockArg;
    Ns_Cond  *condPtr;
    Ns_Mutex *lockPtr;
    Ns_Time   timeout, abstime;
    int       opt, result;

    static CONST char *opts[] = {
        "abswait", "broadcast", "create", "destroy", "set",
        "signal", "wait", NULL
    };
    enum {
        EAbsWaitIdx, EBroadcastIdx, ECreateIdx, EDestroyIdx, ESetIdx,
        ESignalIdx, EWaitIdx
    };
    if (GetArgs(interp, objc, objv, opts, &opt, ECreateIdx, EDestroyIdx,
                condAddr, &condArg, &itPtr->servPtr->tcl.condTable) != TCL_OK) {
        return TCL_ERROR;
    }
    condPtr = (Ns_Cond*) condArg;

    switch (opt) {
    case ECreateIdx:
        Ns_CondInit(condPtr);
        break;

    case EAbsWaitIdx:
    case EWaitIdx:
        if (objc != 4 && objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "condId mutexId ?timeout?");
            return TCL_ERROR;
        }
        if (GetAddr(interp, objv[3], mutexAddr, &lockArg,
                    &itPtr->servPtr->tcl.mutexTable) != TCL_OK) {
            return TCL_ERROR;
        }
        lockPtr = (Ns_Mutex*) lockArg;
        if (objc == 4) {
            timeout.sec = timeout.usec = 0;
        } else if (Ns_TclGetTimeFromObj(interp, objv[4], &timeout) != TCL_OK) {
            return TCL_ERROR;
        }

        if (opt == EAbsWaitIdx) {
            result = Ns_CondTimedWait(condPtr, lockPtr, &timeout);
        } else {
            if (objc == 4 || (timeout.sec == 0 && timeout.usec == 0)) {
                Ns_CondWait(condPtr, lockPtr);
                result = NS_OK;
            } else {
                Ns_GetTime(&abstime);
                Ns_IncrTime(&abstime, timeout.sec, timeout.usec);
                result = Ns_CondTimedWait(condPtr, lockPtr, &abstime);
            }
        }

        if (result == NS_OK) {
            Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
        } else if (result == NS_TIMEOUT) {
            Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
        } else {
            return TCL_ERROR;
        }
        break;

    case EBroadcastIdx:
        Ns_CondBroadcast(condPtr);
        break;

    case ESetIdx:
    case ESignalIdx:
        Ns_CondSignal(condPtr);
        break;

    case EDestroyIdx:
        Ns_CondDestroy(condPtr);
        ns_free(condPtr);
        break;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRWLockObjCmd --
 *
 *      Implements ns_rwlock.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRWLockObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp  *itPtr = (NsInterp *) arg;
    void      *rwlockArg;
    Ns_RWLock *rwlockPtr;
    int        opt, status = TCL_OK;

    static CONST char *opts[] = {
        "create", "destroy", "readlock", "readunlock", "readeval",
        "writelock", "writeunlock", "writeeval", "unlock", NULL
    };
    enum {
        RCreateIdx, RDestroyIdx, RReadLockIdx, RReadUnlockIdx, RReadEvalIdx,
        RWriteLockIdx, RWriteUnlockIdx, RWriteEvalIdx, RUnlockIdx
    };
    if (GetArgs(interp, objc, objv, opts, &opt, RCreateIdx, RDestroyIdx,
                rwlockAddr, &rwlockArg, &itPtr->servPtr->tcl.rwTable) != TCL_OK) {
        return TCL_ERROR;
    }
    rwlockPtr = (Ns_RWLock*) rwlockArg;

    switch (opt) {
    case RCreateIdx:
        Ns_RWLockInit(rwlockPtr);
        break;

    case RReadLockIdx:
        Ns_RWLockRdLock(rwlockPtr);
        break;

    case RWriteLockIdx:
        Ns_RWLockWrLock(rwlockPtr);
        break;

    case RReadUnlockIdx:
    case RWriteUnlockIdx:
    case RUnlockIdx:
        Ns_RWLockUnlock(rwlockPtr);
        break;

    case RReadEvalIdx:
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "script");
            return TCL_ERROR;
        }
        Ns_RWLockRdLock(rwlockPtr);
        status = Tcl_EvalObjEx(interp, objv[3], 0);
        Ns_RWLockUnlock(rwlockPtr);
        break;

    case RWriteEvalIdx:
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "script");
            return TCL_ERROR;
        }
        Ns_RWLockWrLock(rwlockPtr);
        status = Tcl_EvalObjEx(interp, objv[3], 0);
        Ns_RWLockUnlock(rwlockPtr);
        break;

    case RDestroyIdx:
        Ns_RWLockDestroy(rwlockPtr);
        ns_free(rwlockPtr);
        break;
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclThread --
 *
 *      Tcl thread main.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Copy of string result is return as exit arg to be reaped
 *      by ns_thread wait.
 *
 *----------------------------------------------------------------------
 */

void
NsTclThread(void *arg)
{
    TclThreadArg *argPtr = arg;
    Ns_DString    ds, *dsPtr;
    int           detached = argPtr->detached;

    if (detached) {
        dsPtr = NULL;
    } else {
        Ns_DStringInit(&ds);
        dsPtr = &ds;
    }

    /*
     * Need to ensure that the server has completed it's initializtion
     * prior to initiating TclEval.
     */

    Ns_WaitForStartup();

    (void) Ns_TclEval(dsPtr, argPtr->server, argPtr->script);
    ns_free(argPtr);
    if (!detached) {
        Ns_ThreadExit(Ns_DStringExport(&ds));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclThreadArgProc --
 *
 *      Proc info callback to describe a Tcl thread.
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
NsTclThreadArgProc(Tcl_DString *dsPtr, void *arg)
{
    TclThreadArg *argPtr = arg;

    Tcl_DStringAppendElement(dsPtr, argPtr->server);
    if (argPtr->detached) {
        Tcl_DStringAppendElement(dsPtr, "detached");
    }
    Tcl_DStringAppendElement(dsPtr, argPtr->script);
}


/*
 *----------------------------------------------------------------------
 *
 * CreateTclThread --
 *
 *      Create a new Tcl thread.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on Tcl script.
 *
 *----------------------------------------------------------------------
 */

static void
CreateTclThread(NsInterp *itPtr, char *script, int detached, Ns_Thread *thrPtr)
{
    TclThreadArg *argPtr;

    argPtr = ns_malloc(sizeof(TclThreadArg) + strlen(script));
    argPtr->detached = detached;
    strcpy(argPtr->script, script);
    if (itPtr != NULL && itPtr->servPtr != NULL) {
        argPtr->server = itPtr->servPtr->server;
    } else {
        argPtr->server = NULL;
    }
    Ns_ThreadCreate(NsTclThread, argPtr, 0, thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * GetArgs --
 *
 *      Generic argument parser for thread-object commands.
 *
 * Results:
 *      TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *      Memory for a thread-object may be allocated with an Address type
 *      left in the interp's result.
 *
 *----------------------------------------------------------------------
 */

static int
GetArgs(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[],
        CONST char *opts[], int *optPtr, int createOpt, int destroyOpt,
        CONST char *type, void **addrPtr, Tcl_HashTable *table)
{
    Tcl_HashEntry  *hPtr;
    void           *addr;
    int             opt, new;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0, &opt) != TCL_OK) {
        return TCL_ERROR;
    }
    if (opt == createOpt) {
        addr = ns_malloc(sizeof(void *));
        Ns_TclSetAddrObj(Tcl_GetObjResult(interp), type, addr);
    } else {
        if (objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "object");
            return TCL_ERROR;
        }
        if (Ns_TclGetOpaqueFromObj(objv[2], type, &addr) != TCL_OK
            && Ns_TclGetAddrFromObj(interp, objv[2], type, &addr) != TCL_OK) {

            Tcl_ResetResult(interp);
            Ns_MasterLock();
            if (opt == destroyOpt) {
                hPtr = Tcl_FindHashEntry(table, Tcl_GetString(objv[2]));
                if (hPtr == NULL) {
                    Ns_MasterUnlock();
                    Tcl_SetResult(interp,
                        "synchronization object does not exist", TCL_STATIC);
                    return TCL_ERROR;
                }
                addr = Tcl_GetHashValue(hPtr);
                Tcl_DeleteHashEntry(hPtr);
            } else {
                hPtr = Tcl_CreateHashEntry(table, Tcl_GetString(objv[2]), &new);
                if (new) {
                    addr = ns_calloc(1, sizeof(void *));
                    Tcl_SetHashValue(hPtr, addr);
                    Ns_TclSetOpaqueObj(objv[2], type, addr);
                } else {
                    addr = Tcl_GetHashValue(hPtr);
                }
            }
            Ns_MasterUnlock();
        }
    }
    *addrPtr = addr;
    *optPtr = opt;

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetAddr --
 *
 *      Obtain the address-value from the passed object which is
 *      expected to be of the Address type. If the object is not
 *      of the address type, it is converted to one with the new
 *      dinamically allocated address set as the object value.
 *      Note: string rep of the passed object is not invalidated!
 *
 * Results:
 *      TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
GetAddr(Tcl_Interp *interp, Tcl_Obj *argObj, CONST char *name, void **addrPtr,
        Tcl_HashTable *table)
{
    Tcl_HashEntry *hPtr = NULL;

    if (Ns_TclGetOpaqueFromObj(argObj, name, addrPtr) == TCL_OK
        || Ns_TclGetAddrFromObj(interp, argObj, name, addrPtr) == TCL_OK) {
        return TCL_OK;
    }
    Ns_MasterLock();
    hPtr = Tcl_FindHashEntry(table, Tcl_GetString(argObj));
    if (hPtr) {
        *addrPtr = Tcl_GetHashValue(hPtr);
    }
    Ns_MasterUnlock();

    return (hPtr == NULL) ? TCL_ERROR : TCL_OK;
}

