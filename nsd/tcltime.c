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
 * tcltime.c --
 *
 *      A Tcl interface to the Ns_Time microsecond resolution time
 *      routines and some time formatting commands.
 */

#include "nsd.h"

NS_RCSID("@(#) $Header$");


/*
 * Local functions defined in this file
 */

static void  SetTimeInternalRep(Tcl_Obj *objPtr, Ns_Time *timePtr);
static int   SetTimeFromAny (Tcl_Interp *interp, Tcl_Obj *objPtr);
static void  UpdateStringOfTime(Tcl_Obj *objPtr);

/*
 * Local variables defined in this file.
 */

static Tcl_ObjType timeType = {
    "ns:time",
    (Tcl_FreeInternalRepProc *) NULL,
    (Tcl_DupInternalRepProc *) NULL,
    UpdateStringOfTime,
    SetTimeFromAny
};

static Tcl_ObjType *intTypePtr;



/*
 *----------------------------------------------------------------------
 *
 * NsTclInitTimeType --
 *
 *      Initialize Ns_Time Tcl_Obj type.
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
NsTclInitTimeType()
{
    Tcl_Obj obj;

    if (sizeof(obj.internalRep) < sizeof(Ns_Time)) {
        Tcl_Panic("NsTclInitObjs: sizeof(obj.internalRep) < sizeof(Ns_Time)");
    }
    if (sizeof(int) < sizeof(long)) {
        Tcl_Panic("NsTclInitObjs: sizeof(int) < sizeof(long)");
    }
    intTypePtr = Tcl_GetObjType("int");
    if (intTypePtr == NULL) {
        Tcl_Panic("NsTclInitObjs: no int type");
    }
    Tcl_RegisterObjType(&timeType);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclSetTimeObj --
 *
 *      Set a Tcl_Obj to an Ns_Time object.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      String rep is invalidated and internal rep is set.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclSetTimeObj(Tcl_Obj *objPtr, Ns_Time *timePtr)
{
    Tcl_InvalidateStringRep(objPtr);
    SetTimeInternalRep(objPtr, timePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetTimeFromObj --
 *
 *      Return the internal value of an Ns_Time Tcl_Obj.
 *
 * Results:
 *      TCL_OK or TCL_ERROR if not a valid Ns_Time.
 *
 * Side effects:
 *      Object is converted to Ns_Time type if necessary.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclGetTimeFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Ns_Time *timePtr)
{
    if (objPtr->typePtr == intTypePtr) {
        if (Tcl_GetLongFromObj(interp, objPtr, &timePtr->sec) != TCL_OK) {
            return TCL_ERROR;
        }
        timePtr->usec = 0;
    } else {
        if (Tcl_ConvertToType(interp, objPtr, &timeType) != TCL_OK) {
            return TCL_ERROR;
        }
        *timePtr = *((Ns_Time *) &objPtr->internalRep);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTimeObjCmd --
 *
 *      Implements ns_time. 
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
NsTclTimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Ns_Time result, t1, t2;
    int opt;

    static CONST char *opts[] = {
        "adjust", "diff", "get", "incr", "make",
        "seconds", "microseconds", NULL
    };
    enum {
        TAdjustIdx, TDiffIdx, TGetIdx, TIncrIdx, TMakeIdx,
        TSecondsIdx, TMicroSecondsIdx
    };

    if (objc < 2) {
        Tcl_SetLongObj(Tcl_GetObjResult(interp), time(NULL));
        return TCL_OK;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                            &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (opt) {
    case TGetIdx:
        Ns_GetTime(&result);
        break;

    case TMakeIdx:
        if (objc != 3 && objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "sec ?usec?");
            return TCL_ERROR;
        }
        if (Tcl_GetLongFromObj(interp, objv[2], &result.sec) != TCL_OK) {
            return TCL_ERROR;
        }
        if (objc == 3) {
            result.usec = 0;
        } else if (Tcl_GetLongFromObj(interp, objv[3], &result.usec) != TCL_OK) {
            return TCL_ERROR;
        }
        break;

    case TIncrIdx:
        if (objc != 4 && objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "time sec ?usec?");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK ||
            Tcl_GetLongFromObj(interp, objv[3], &t2.sec) != TCL_OK) {
            return TCL_ERROR;
        }
        if (objc == 4) {
            t2.usec = 0;
        } else if (Tcl_GetLongFromObj(interp, objv[4], &t2.usec) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_IncrTime(&result, t2.sec, t2.usec);
        break;

    case TDiffIdx:
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "time1 time2");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &t1) != TCL_OK ||
            Ns_TclGetTimeFromObj(interp, objv[3], &t2) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_DiffTime(&t1, &t2, &result);
        break;

    case TAdjustIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "time");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_AdjTime(&result);
        break;

    case TSecondsIdx:
    case TMicroSecondsIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "time");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetLongObj(Tcl_GetObjResult(interp),
                       opt == TSecondsIdx ? result.sec : result.usec);
        return TCL_OK;
        break;
    }
    Ns_TclSetTimeObj(Tcl_GetObjResult(interp), &result);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLocalTimeObjCmd, NsTclGmTimeObjCmd --
 *
 *      Implements ns_gmtime and ns_localtime.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      ns_localtime depends on the time zone of the server process.
 *
 *----------------------------------------------------------------------
 */

static int
TmObjCmd(ClientData isgmt, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    time_t     now;
    struct tm *ptm;
    Tcl_Obj   *objPtr[9];

    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
    now = time(NULL);
    if (isgmt) {
        ptm = ns_gmtime(&now);
    } else {
        ptm = ns_localtime(&now);
    }
    objPtr[0] = Tcl_NewIntObj(ptm->tm_sec);
    objPtr[1] = Tcl_NewIntObj(ptm->tm_min);
    objPtr[2] = Tcl_NewIntObj(ptm->tm_hour);
    objPtr[3] = Tcl_NewIntObj(ptm->tm_mday);
    objPtr[4] = Tcl_NewIntObj(ptm->tm_mon);
    objPtr[5] = Tcl_NewIntObj(ptm->tm_year);
    objPtr[6] = Tcl_NewIntObj(ptm->tm_wday);
    objPtr[7] = Tcl_NewIntObj(ptm->tm_yday);
    objPtr[8] = Tcl_NewIntObj(ptm->tm_isdst);
    Tcl_SetListObj(Tcl_GetObjResult(interp), 9, objPtr);

    return TCL_OK;
}

int
NsTclGmTimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    return TmObjCmd((ClientData) 1, interp, objc, objv);
}

int
NsTclLocalTimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    return TmObjCmd(NULL, interp, objc, objv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSleepObjCmd --
 *
 *      Sleep with milisecond resolition.
 *
 * Results:
 *      Tcl Result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclSleepObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Ns_Time time;
    int     ms;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "timespec");
        return TCL_ERROR;
    }
    if (Ns_TclGetTimeFromObj(interp, objv[1], &time) != TCL_OK) {
        return TCL_ERROR;
    }
    Ns_AdjTime(&time);
    if (time.sec < 0 || (time.sec == 0 && time.usec < 0)) {
        Tcl_AppendResult(interp, "invalid timespec: ", 
                         Tcl_GetString(objv[1]), NULL);
        return TCL_ERROR;
    }
    ms = time.sec * 1000 + time.usec / 1000;
    Tcl_Sleep(ms);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStrftimeObjCmd --
 *
 *      Implements ns_fmttime. 
 *
 * Results:
 *      Tcl result. 
 *
 * Side effects:
 *      Depends on the time zone of the server process.
 *
 *----------------------------------------------------------------------
 */

int
NsTclStrftimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    char   *fmt, buf[200];
    time_t  time;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "time ?fmt?");
        return TCL_ERROR;
    }
    if (Tcl_GetLongFromObj(interp, objv[1], &time) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc > 2) {
        fmt = Tcl_GetString(objv[2]);
    } else {
        fmt = "%c";
    }
    if (strftime(buf, sizeof(buf), fmt, ns_localtime(&time)) == 0) {
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "invalid time: ", 
                               Tcl_GetString(objv[1]), NULL);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, buf, TCL_VOLATILE);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * UpdateStringOfTime --
 *
 *      Update the string representation for an Ns_Time object.
 *      Note: This procedure does not free an existing old string rep
 *      so storage will be lost if this has not already been done. 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The object's string is set to a valid string that results from
 *      the Ns_Time-to-string conversion.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateStringOfTime(Tcl_Obj *objPtr)
{
    Ns_Time *timePtr = (Ns_Time *) &objPtr->internalRep;
    int      len;
    char     buf[100];

    Ns_AdjTime(timePtr);
    if (timePtr->usec == 0) {
        len = sprintf(buf, "%ld", timePtr->sec);
    } else {
        len = sprintf(buf, "%ld:%ld", timePtr->sec, timePtr->usec);
    }
    Ns_TclSetStringRep(objPtr, buf, len);
}


/*
 *----------------------------------------------------------------------
 *
 * SetTimeFromAny --
 *
 *      Attempt to generate an Ns_Time internal form for the Tcl object.
 *
 * Results:
 *      The return value is a standard object Tcl result. If an error occurs
 *      during conversion, an error message is left in the interpreter's
 *      result unless "interp" is NULL.
 *
 * Side effects:
 *      If no error occurs, an int is stored as "objPtr"s internal
 *      representation. 
 *
 *----------------------------------------------------------------------
 */

static int
SetTimeFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    char    *str, *sep;
    Ns_Time  time;
    int      result;

    str = Tcl_GetString(objPtr);
    sep = strchr(str, ':');
    if (objPtr->typePtr == intTypePtr || sep == NULL) {
        if (Tcl_GetLongFromObj(interp, objPtr, &time.sec) != TCL_OK) {
            return TCL_ERROR;
        }
        time.usec = 0;
    } else {
        *sep = '\0';
        result = Tcl_GetInt(interp, str, (int *) &time.sec);
        *sep = ':';
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
        if (Tcl_GetInt(interp, sep+1, (int *) &time.usec) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    Ns_AdjTime(&time);
    SetTimeInternalRep(objPtr, &time);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SetTimeInternalRep --
 *
 *      Set the internal Ns_Time, freeing a previous internal rep if
 *      necessary.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Object will be an Ns_Time type.
 *
 *----------------------------------------------------------------------
 */

static void
SetTimeInternalRep(Tcl_Obj *objPtr, Ns_Time *timePtr)
{
    Ns_TclResetObjType(objPtr, &timeType);
    *((Ns_Time *) &objPtr->internalRep) = *timePtr;
    Tcl_InvalidateStringRep(objPtr);
    objPtr->length = 0;  /* ensure there's no stumbling */
}