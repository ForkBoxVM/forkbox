/* $Id: RTProcQueryExecutablePath-nt.cpp 111211 2025-10-02 11:20:45Z alexander.eichner@oracle.com $ */
/** @file
 * IPRT - RTProcQueryExecutablePath, Native NT.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_FILE
#include <iprt/nt/nt-and-windows.h>
#include "internal-r3-nt.h"

#include <iprt/process.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>


/**
 * Worker trying to acquire the image filename for the given process ID.
 *
 * @returns IPRT status code.
 * @param   hProcess        The process ID to query the filename for.
 * @param   ppbBuf          Where to store the pointer to the buffer containing the
 *                          UNICODE_STRING struct of the filename on success, free with
 *                          RTMemTmpFree().
 */
static int rtProcQueryExecutablePathWorker(RTPROCESS hProcess, uint8_t **ppbBuf)
{
    int rc = VINF_SUCCESS;
    HANDLE hNtProcess = INVALID_HANDLE_VALUE;
    NTSTATUS rcNt = ERROR_SUCCESS;
    if (   hProcess == NIL_RTPROCESS
        || hProcess == RTProcSelf())
        hNtProcess = NtCurrentProcess();
    else
    {
        CLIENT_ID ClientId;
        ClientId.UniqueProcess = (HANDLE)(uintptr_t)hProcess;
        ClientId.UniqueThread  = NULL;

        OBJECT_ATTRIBUTES ObjAttrs;
        InitializeObjectAttributes(&ObjAttrs, NULL, OBJ_CASE_INSENSITIVE, NULL, NULL);

        rcNt = NtOpenProcess(&hNtProcess, PROCESS_QUERY_LIMITED_INFORMATION, &ObjAttrs, &ClientId);
        if (!NT_SUCCESS(rcNt))
            rcNt = NtOpenProcess(&hNtProcess, PROCESS_QUERY_INFORMATION, &ObjAttrs, &ClientId);
    }

    if (NT_SUCCESS(rcNt))
    {
        ULONG   cbBuf  = _1K;
        uint8_t *pbBuf = (uint8_t *)RTMemTmpAlloc(cbBuf);
        if (pbBuf)
        {
            ULONG cbNeeded;
            rcNt = NtQueryInformationProcess(hNtProcess, ProcessImageFileName, pbBuf, cbBuf, &cbNeeded);
            if (rcNt == STATUS_INFO_LENGTH_MISMATCH)
            {
                RTMemTmpFree(pbBuf);
                pbBuf = (uint8_t *)RTMemTmpAlloc(cbNeeded);
                if (pbBuf)
                {
                    cbBuf = cbNeeded;
                    rcNt = NtQueryInformationProcess(hNtProcess, ProcessImageFileName, pbBuf, cbBuf, NULL);
                }
                else
                    rc = VERR_NO_TMP_MEMORY;
            }

            if (   NT_SUCCESS(rcNt)
                && RT_SUCCESS(rc))
                *ppbBuf = pbBuf;
            else if (RT_SUCCESS(rc))
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }
    else
        rc = RTErrConvertFromNtStatus(rcNt);

    if (   hNtProcess != NtCurrentProcess()
        && hNtProcess != INVALID_HANDLE_VALUE)
        NtClose(hNtProcess);

    return rc;
}


RTR3DECL(int) RTProcQueryExecutablePath(RTPROCESS hProcess, char *pszExecPath, size_t cbExecPath, size_t *pcbExecPath)
{
    AssertReturn(   (pszExecPath && cbExecPath > 0)
                 || (!pszExecPath && !cbExecPath), VERR_INVALID_PARAMETER);
    AssertReturn(pcbExecPath || pszExecPath, VERR_INVALID_PARAMETER);

    uint8_t *pbBuf;
    int rc = rtProcQueryExecutablePathWorker(hProcess, &pbBuf);
    if (RT_SUCCESS(rc))
    {
        UNICODE_STRING *pNtNameProc = (UNICODE_STRING *)pbBuf;

        *pcbExecPath = RTUtf16CalcUtf8Len(pNtNameProc->Buffer) + 1;
        if (*pcbExecPath <= cbExecPath)
        {
            rc = RTUtf16ToUtf8Ex(pNtNameProc->Buffer, RTSTR_MAX, &pszExecPath, cbExecPath, pcbExecPath);
            if (pcbExecPath)
                *pcbExecPath += 1;
        }
        else
            rc = VERR_BUFFER_OVERFLOW;

        RTMemTmpFree(pbBuf);
    }

    return rc;
}


RTR3DECL(int) RTProcQueryExecutablePathA(RTPROCESS hProcess, char **ppszExecPath)
{
    AssertPtrReturn(ppszExecPath, VERR_INVALID_POINTER);

    uint8_t *pbBuf;
    int rc = rtProcQueryExecutablePathWorker(hProcess, &pbBuf);
    if (RT_SUCCESS(rc))
    {
        UNICODE_STRING *pNtNameProc = (UNICODE_STRING *)pbBuf;
        rc = RTUtf16ToUtf8(pNtNameProc->Buffer, ppszExecPath);
        RTMemTmpFree(pbBuf);
    }

    return rc;
}

