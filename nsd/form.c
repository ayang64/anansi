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
 * form.c --
 *
 *      Routines for dealing with HTML FORM's.
 */

static const char *RCSID = "@(#) $Header$, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static void ParseQuery(char *form, Ns_Set *set, Tcl_Encoding encoding);
static char *Decode(Tcl_DString *dsPtr, char *s, Tcl_Encoding encoding);
static char *Ext2Utf(Tcl_DString *dsPtr, char *s, int len, Tcl_Encoding encoding);
static int GetBoundary(Tcl_DString *dsPtr, Ns_Conn *conn);
static char * NextBoundry(Tcl_DString *dsPtr, char *s, char *e);
static void AttEnd(char **sPtr, char **ePtr);
static void ParseMultiInput(Conn *connPtr, char *start, char *end);


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetQuery --
 *
 *	Get the connection query data, either by reading the content 
 *	of a POST request or get it from the query string 
 *
 * Results:
 *	Query data or NULL if error 
 *
 * Side effects:
 *	
 *
 *----------------------------------------------------------------------
 */

Ns_Set  *
Ns_ConnGetQuery(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;
    Tcl_DString	    bound;
    char	   *s, *e, *form, *formend;
    
    if (connPtr->query == NULL) {
	connPtr->query = Ns_SetCreate(NULL);
	if (!STREQ(connPtr->request->method, "POST")) {
	    form = connPtr->request->query;
	    if (form != NULL) {
		ParseQuery(form, connPtr->query, connPtr->encoding);
	    }
	} else if ((form = connPtr->reqPtr->content) != NULL) {
	    Tcl_DStringInit(&bound);
	    if (!GetBoundary(&bound, conn)) {
		ParseQuery(form, connPtr->query, connPtr->encoding);
	    } else {
	    	formend = form + connPtr->reqPtr->length;
		s = NextBoundry(&bound, form, formend);
		while (s != NULL) {
		    s += bound.length;
		    if (*s == '\r') ++s;
		    if (*s == '\n') ++s;
		    e = NextBoundry(&bound, s, formend);
		    if (e != NULL) {
			ParseMultiInput(connPtr, s, e);
		    }
		    s = e;
		}
	    }
	    Tcl_DStringFree(&bound);
	}
    }
    return connPtr->query;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_QueryToSet --
 *
 *	Parse query data into an Ns_Set 
 *
 * Results:
 *	NS_OK. 
 *
 * Side effects:
 *	Will add data to set without any UTF conversion.
 *
 *----------------------------------------------------------------------
 */

int
Ns_QueryToSet(char *query, Ns_Set *set)
{
    ParseQuery(query, set, NULL);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclParseQueryCmd --
 *
 *	This procedure implements the AOLserver Tcl
 *
 *	    ns_parsequery querystring
 *
 *	command.
 *
 * Results:
 *	The Tcl result is a Tcl set with the parsed name-value pairs from
 *	the querystring argument
 *
 * Side effects:
 *	None external.
 *
 *----------------------------------------------------------------------
 */

int
NsTclParseQueryCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv) 
{
    Ns_Set *set;

    if (argc != 2) {
	Tcl_AppendResult(interp, argv[0], ": wrong number args: should be \"",
	    argv[0], " querystring\"", (char *) NULL);
	return TCL_ERROR;
    }
    set = Ns_SetCreate(NULL);
    if (Ns_QueryToSet(argv[1], set) != NS_OK) {
	Tcl_AppendResult(interp, argv[0], ": could not parse: \"",
	    argv[1], "\"", (char *) NULL);
	Ns_SetFree(set);
	return TCL_ERROR;
    }
    return Ns_TclEnterSet(interp, set, NS_TCL_SET_DYNAMIC);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclParseQueryObjCmd --
 *
 *	This procedure implements the AOLserver Tcl
 *
 *	    ns_parsequery querystring
 *
 *	command.
 *
 * Results:
 *	The Tcl result is a Tcl set with the parsed name-value pairs from
 *	the querystring argument
 *
 * Side effects:
 *	None external.
 *
 *----------------------------------------------------------------------
 */

int
NsTclParseQueryObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Ns_Set *set;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "querystring");
	return TCL_ERROR;
    }
    set = Ns_SetCreate(NULL);
    if (Ns_QueryToSet(Tcl_GetString(objv[1]), set) != NS_OK) {
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
		"could not parse: \"", Tcl_GetString(objv[1]), "\"", NULL);
	Ns_SetFree(set);
	return TCL_ERROR;
    }
    return Ns_TclEnterSet(interp, set, NS_TCL_SET_DYNAMIC);
}


/*
 *----------------------------------------------------------------------
 *
 * ParseQuery --
 *
 *	Parse the given form string for URL encoded key=value pairs,
 *	converting to UTF if given encoding is not NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static void
ParseQuery(char *form, Ns_Set *set, Tcl_Encoding encoding)
{
    char *p, *k, *v;
    Tcl_DString      kds, vds;

    Tcl_DStringInit(&kds);
    Tcl_DStringInit(&vds);
    p = form;
    while (p != NULL) {
	k = p;
	p = strchr(p, '&');
	if (p != NULL) {
	    *p = '\0';
	}
	v = strchr(k, '=');
	if (v != NULL) {
	    *v = '\0';
	}
	k = Decode(&kds, k, encoding);
	if (v != NULL) {
	    Decode(&vds, v+1, encoding);
	    *v = '=';
	    v = vds.string;
	}
	Ns_SetPut(set, k, v);
	if (p != NULL) {
	    *p++ = '&';
	}
    }
    Tcl_DStringFree(&kds);
    Tcl_DStringFree(&vds);
}


/*
 *----------------------------------------------------------------------
 *
 * ParseMulitInput --
 *
 *	Parse the a multipart form input.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Records offset, lengths for files.
 *
 *----------------------------------------------------------------------
 */

static void
ParseMultiInput(Conn *connPtr, char *start, char *end)
{
    Tcl_Encoding encoding = connPtr->encoding;
    Tcl_DString kds, vds;
    char *s, *e, *ks, *ke, *fs, *fe, save, saveend;
    char *key, *value, *ts, *te;

    Tcl_DStringInit(&kds);
    Tcl_DStringInit(&vds);

    /*
     * Trim off the trailing \r\n and null terminate the input.
     */

    if (end > start && end[-1] == '\n') --end;
    if (end > start && end[-1] == '\r') --end;
    saveend = *end;
    *end = '\0';

    /*
     * Scan non-blank lines for the content-disposition header.
     */

    ts = ks = fs = NULL;
    while ((e = strchr(start, '\n')) != NULL) {
	s = start;
	start = e + 1;
	if (e > s && e[-1] == '\r') {
	    --e;
	}
	if (s == e) {
	    break;
	}
	save = *e;
	*e = '\0';
	if (strncasecmp(s, "content-disposition", 19) == 0
	    && (ks = Ns_StrCaseFind(s+19, "name=")) != NULL) {
	    /*
	     * Save the key name and filename.
	     */

	    ks += 5;
	    AttEnd(&ks, &ke);
	    if ((fs = Ns_StrCaseFind(s+19, "filename=")) != NULL) {
		fs += 9;
		AttEnd(&fs, &fe);
	    }
	} else if (strncasecmp(s, "content-type:", 13) == 0) {
	    ts = s+13;
	    while (isspace(UCHAR(*ts))) {
		++ts;
	    }
	    te = ts;
	    AttEnd(&ts, &te);
	}
	*e = save;
    }

    /*
     * Save the key/value and file contents if found.
     */

    if (ks != NULL) {
	key = Ext2Utf(&kds, ks, ke-ks, encoding);
	if (fs == NULL) {
	    value = Ext2Utf(&vds, start, end-start, encoding);
	} else {
	    Tcl_DStringAppendElement(&connPtr->files, key);
	    Ns_DStringPrintf(&connPtr->files, " %d %d",
		start - connPtr->reqPtr->content, end - start);
	    if (ts == NULL) {
		ts = "";
	    } else {
	    	ts = Ext2Utf(&vds, ts, te-ts, encoding);
	    }
	    Tcl_DStringAppendElement(&connPtr->files, ts);
	    value = Ext2Utf(&vds, fs, fe-fs, encoding);
	}
	Ns_SetPut(connPtr->query, key, value);
    }

    /*
     * Restore the end marker.
     */

    *end = saveend;
    Tcl_DStringFree(&kds);
    Tcl_DStringFree(&vds);
}


/*
 *----------------------------------------------------------------------
 *
 * Decode --
 *
 *	Decode a form key or value, converting + to spaces,
 *	UrlDecode, and convert to UTF if given and encoding.
 *
 * Results:
 *	Pointer to dsPtr->string.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static char *
Decode(Tcl_DString *dsPtr, char *s, Tcl_Encoding encoding)
{
    Tcl_DString uds, eds;

    Tcl_DStringInit(&uds);
    Tcl_DStringInit(&eds);

    /*
     * Convert +'s, if any, to spaces.
     */

    if (strchr(s, '+') != NULL) {
	s = Tcl_DStringAppend(&uds, s, -1);
	while (*s != '\0') {
	    if (*s == '+') {
		*s = ' ';
	    }
	    ++s;
	}
	s = uds.string;
    }

    /*
     * URL decode and then convert to UTF.
     */

    Ns_DecodeUrl(&eds, s);
    Ext2Utf(dsPtr, eds.string, eds.length, encoding);
    Tcl_DStringFree(&uds);
    Tcl_DStringFree(&eds);
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * GetBoundary --
 *
 *	Copy multipart/form-data boundy string, if any.
 *
 * Results:
 *	1 if boundy copied, 0 otherwise.
 *
 * Side effects:
 *	Copies boundry string to given dstring.
 *
 *----------------------------------------------------------------------
 */

static int
GetBoundary(Tcl_DString *dsPtr, Ns_Conn *conn)
{
    char *type, *bs, *be;

    type = Ns_SetIGet(conn->headers, "content-type");
    if (type != NULL
	&& Ns_StrCaseFind(type, "multipart/form-data") != NULL
	&& (bs = Ns_StrCaseFind(type, "boundary=")) != NULL) {
	bs += 9;
	be = bs;
	while (*be && !isspace(UCHAR(*be))) {
	    ++be;
	}
	Tcl_DStringAppend(dsPtr, "--", 2);
	Tcl_DStringAppend(dsPtr, bs, be-bs);
	return 1;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * NextBoundary --
 *
 *	Locate the next form boundry.
 *
 * Results:
 *	Pointer to start of next input field or NULL on end of fields.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
NextBoundry(Tcl_DString *dsPtr, char *s, char *e)
{
    char c, sc, *find;
    size_t len;

    find = dsPtr->string;
    c = *find++;
    len = dsPtr->length-1;
    e -= len;
    do {
	do {
	    sc = *s++;
	    if (s > e) {
		return NULL;
	    }
	} while (sc != c);
    } while (strncmp(s, find, len) != 0);
    s--;
    return s;
}


/*
 *----------------------------------------------------------------------
 *
 * AttEnd --
 *
 *	Determine start and end of a multipart form input attribute.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Start and end are stored in given pointers.
 *
 *----------------------------------------------------------------------
 */

static void
AttEnd(char **sPtr, char **ePtr)
{
    char *s, *e;

    s = *sPtr;
    e = s;
    while (*e && !isspace(UCHAR(*e))) {
	++e;
    }
    if (e > s && e[-1] == ';') {
	--e;
    }
    if (*s == '"' && (e - s) > 1 && e[-1] == '"') {
	++s;
	--e;
    }
    *sPtr = s;
    *ePtr = e;
}


/*
 *----------------------------------------------------------------------
 *
 * Ext2Utf --
 *
 *	Convert input string to UTF.
 *
 * Results:
 *	Pointer to converted string.
 *
 * Side effects:
 *	Converted string is copied to given dstring, overwriting
 *	any previous content.
 *
 *----------------------------------------------------------------------
 */

static char *
Ext2Utf(Tcl_DString *dsPtr, char *start, int len, Tcl_Encoding encoding)
{
    if (encoding == NULL) {
	Tcl_DStringTrunc(dsPtr, 0);
	Tcl_DStringAppend(dsPtr, start, len);
    } else {
	/* NB: ExternalToUtfDString will re-init dstring. */
	Tcl_DStringFree(dsPtr);
	Tcl_ExternalToUtfDString(encoding, start, len, dsPtr);
    }
    return dsPtr->string;
}
