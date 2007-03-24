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
 * progress.c --
 *
 *      Track the progress of large uploads.
 */

#include "nsd.h"

NS_RCSID("@(#) $Header$");


/*
 * The following structure tracks the progress of a single upload.
 */

typedef struct Progress {
    unsigned long  current;   /* Uploaded so far. */
    unsigned long  size;      /* Total bytes to upload. */
    Tcl_HashEntry *hPtr;      /* Our entry in the url table. */
} Progress;


/*
 * Static functions defined in this file.
 */

static Ns_Callback ResetProgress;


/*
 * Static variables defined in this file.
 */

static int           progressMinSize; /* Config: progress enabled? */

static Ns_Sls        slot;            /* Per-socket progress slot. */

static Tcl_HashTable urlTable;        /* Large uploads in progress. */
static Ns_Mutex      lock;            /* Lock around table and Progress struct. */



/*
 *----------------------------------------------------------------------
 *
 * NsConfigProgress --
 *
 *      Initialise the progress susbsystem at server startup.
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
NsConfigProgress(void)
{
    progressMinSize =
        Ns_ConfigIntRange(NS_CONFIG_PARAMETERS, "progressminsize", 0, 0, INT_MAX);

    if (progressMinSize > 0) {
        Ns_SlsAlloc(&slot, ResetProgress);
        Tcl_InitHashTable(&urlTable, TCL_STRING_KEYS);
        Ns_MutexSetName(&lock, "ns:progress");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsUpdateProgress --
 *
 *      Note the current progress of a large upload. Called repeatedly
 *      untill all bytes have been read.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A Progress structure is allocated the first time a sock's
 *      progress is updated.
 *
 *----------------------------------------------------------------------
 */

void
NsUpdateProgress(Ns_Sock *sock)
{
    Sock       *sockPtr = (Sock *) sock;
    Request    *reqPtr  = sockPtr->reqPtr;
    Ns_Request *request = reqPtr->request;
    Progress   *pPtr;
    Ns_DString  ds;
    int         new;

    if (progressMinSize > 0 && reqPtr->length > progressMinSize) {

        pPtr = Ns_SlsGet(&slot, sock);

        if (pPtr == NULL) {
            pPtr = ns_calloc(1, sizeof(Progress));
            Ns_SlsSet(&slot, sock, pPtr);
        }

        if (pPtr->hPtr == NULL) {
            pPtr->size = reqPtr->length;
            pPtr->current = reqPtr->avail;

            Ns_DStringInit(&ds);
            Ns_DStringAppend(&ds, request->url);
            if (request->query != NULL) {
                Ns_DStringAppend(&ds, "?");
                Ns_DStringAppend(&ds, request->query);
            }

            Ns_MutexLock(&lock);
            pPtr->hPtr = Tcl_CreateHashEntry(&urlTable, Ns_DStringValue(&ds), &new);
            Tcl_SetHashValue(pPtr->hPtr, pPtr);
            Ns_MutexUnlock(&lock);

            Ns_DStringFree(&ds);

        } else {
            Ns_MutexLock(&lock);
            pPtr->current = reqPtr->avail;
            Ns_MutexUnlock(&lock);
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUploadStatsObjCmd --
 *
 *      Get the progress so far and total bytes to upload for the given
 *      unique URL as a two element list.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclProgressObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_HashEntry *hPtr;
    Tcl_Obj       *resObj;
    Progress      *pPtr;
    char          *url;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "url");
        return TCL_ERROR;
    }

    url = Tcl_GetString(objv[1]);

    Ns_MutexLock(&lock);
    hPtr = Tcl_FindHashEntry(&urlTable, url);
    if (hPtr != NULL) {
        pPtr = Tcl_GetHashValue(hPtr);
        resObj = Tcl_GetObjResult(interp);
        if (Tcl_ListObjAppendElement(interp, resObj,
                                    Tcl_NewLongObj(pPtr->current)) != TCL_OK
            || Tcl_ListObjAppendElement(interp, resObj,
                                        Tcl_NewLongObj(pPtr->size)) != TCL_OK) {
            Ns_MutexUnlock(&lock);
            return TCL_ERROR;
        }
    }
    Ns_MutexUnlock(&lock);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ResetProgress --
 *
 *      Reset the progress of a connection when the Ns_Sock is
 *      pushed back on the free list.  The Progress struct is reused.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
ResetProgress(void *arg)
{
    Progress *pPtr = arg;

    if (pPtr->hPtr) {
        Ns_MutexLock(&lock);
        Tcl_DeleteHashEntry(pPtr->hPtr);
        pPtr->hPtr = NULL;
        pPtr->current = 0;
        pPtr->size = 0;
        Ns_MutexUnlock(&lock);
    }
}