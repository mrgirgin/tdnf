/*
 * Copyright (C) 2015-2017 VMware, Inc. All Rights Reserved.
 *
 * Licensed under the GNU Lesser General Public License v2.1 (the "License");
 * you may not use this file except in compliance with the License. The terms
 * of the License are located in the COPYING file of this distribution.
 */

/*
 * Module   : rpmtrans.c
 *
 * Abstract :
 *
 *            tdnfclientlib
 *
 *            client library
 *
 * Authors  : Priyesh Padmavilasom (ppadmavilasom@vmware.com)
 */

#include "includes.h"

uint32_t
TDNFRpmExecTransaction(
    PTDNF pTdnf,
    PTDNF_SOLVED_PKG_INFO pSolvedInfo,
    TDNF_ALTERTYPE nAlterType
    )
{
    uint32_t dwError = 0;
    int nKeepCachedRpms = 0;
    TDNFRPMTS ts = {0};

    if(!pTdnf || !pTdnf->pArgs || !pTdnf->pConf || !pSolvedInfo)
    {
        dwError = ERROR_TDNF_INVALID_PARAMETER;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    ts.nQuiet = pTdnf->pArgs->nQuiet;
    nKeepCachedRpms = pTdnf->pConf->nKeepCache;

    dwError = TDNFAllocateMemory(
                  1,
                  sizeof(TDNF_CACHED_RPM_LIST),
                  (void**)&ts.pCachedRpmsArray);
    BAIL_ON_TDNF_ERROR(dwError);

    rpmSetVerbosity(TDNFConfGetRpmVerbosity(pTdnf));

    //Allow downgrades
    ts.nProbFilterFlags = RPMPROB_FILTER_OLDPACKAGE;
    if(nAlterType == ALTER_REINSTALL)
    {
        ts.nProbFilterFlags = ts.nProbFilterFlags | RPMPROB_FILTER_REPLACEPKG;
    }

    ts.pTS = rpmtsCreate();
    if(!ts.pTS)
    {
        dwError = ERROR_TDNF_RPMTS_CREATE_FAILED;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    ts.nTransFlags = rpmtsSetFlags (ts.pTS, RPMTRANS_FLAG_NONE);

    if(rpmtsSetRootDir (ts.pTS, pTdnf->pArgs->pszInstallRoot))
    {
        dwError = ERROR_TDNF_RPMTS_BAD_ROOT_DIR;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    if(rpmtsSetNotifyCallback(ts.pTS, TDNFRpmCB, (void*)&ts))
    {
        dwError = ERROR_TDNF_RPMTS_SET_CB_FAILED;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    dwError = TDNFPopulateTransaction(&ts, pTdnf, pSolvedInfo);
    BAIL_ON_TDNF_ERROR(dwError);

    dwError = TDNFRunTransaction(&ts, pTdnf);
    BAIL_ON_TDNF_ERROR(dwError);

cleanup:
    if(ts.pTS)
    {
        rpmtsCloseDB(ts.pTS);
        rpmtsFree(ts.pTS);
    }
    if(ts.pCachedRpmsArray)
    {
        if(!nKeepCachedRpms)
        {
            TDNFRemoveCachedRpms(ts.pCachedRpmsArray);
        }
        TDNFFreeCachedRpmsArray(ts.pCachedRpmsArray);
    }
    return dwError;

error:
    goto cleanup;
}

uint32_t
TDNFTransAddErasePkgs(
    PTDNFRPMTS pTS,
    PTDNF_PKG_INFO pInfo)
{
    uint32_t dwError = 0;
    if(!pInfo)
    {
        dwError = ERROR_TDNF_NO_DATA;
        BAIL_ON_TDNF_ERROR(dwError);
    }
    while(pInfo)
    {
        dwError = TDNFTransAddErasePkg(pTS, pInfo->pszName);
        pInfo = pInfo->pNext;
        BAIL_ON_TDNF_ERROR(dwError);
    }

cleanup:
    return dwError;

error:
    if(dwError == ERROR_TDNF_NO_DATA)
    {
        dwError = 0;
    }
    goto cleanup;
}


uint32_t
TDNFPopulateTransaction(
    PTDNFRPMTS pTS,
    PTDNF pTdnf,
    PTDNF_SOLVED_PKG_INFO pSolvedInfo
    )
{
    uint32_t dwError = 0;
    if(pSolvedInfo->pPkgsToInstall)
    {
        dwError = TDNFTransAddInstallPkgs(
                      pTS,
                      pTdnf,
                      pSolvedInfo->pPkgsToInstall);
        BAIL_ON_TDNF_ERROR(dwError);
    }
    if(pSolvedInfo->pPkgsToReinstall)
    {
        dwError = TDNFTransAddInstallPkgs(
                      pTS,
                      pTdnf,
                      pSolvedInfo->pPkgsToReinstall);
        BAIL_ON_TDNF_ERROR(dwError);
    }
    if(pSolvedInfo->pPkgsToUpgrade)
    {
        dwError = TDNFTransAddUpgradePkgs(
                      pTS,
                      pTdnf,
                      pSolvedInfo->pPkgsToUpgrade);
        BAIL_ON_TDNF_ERROR(dwError);
    }
    if(pSolvedInfo->pPkgsToRemove)
    {
        dwError = TDNFTransAddErasePkgs(
                      pTS,
                      pSolvedInfo->pPkgsToRemove);
        BAIL_ON_TDNF_ERROR(dwError);
    }
    if(pSolvedInfo->pPkgsObsoleted)
    {
        dwError = TDNFTransAddObsoletedPkgs(
                      pTS,
                      pSolvedInfo->pPkgsObsoleted);
        BAIL_ON_TDNF_ERROR(dwError);
    }
    if(pSolvedInfo->pPkgsToDowngrade)
    {
        dwError = TDNFTransAddInstallPkgs(
                      pTS,
                      pTdnf,
                      pSolvedInfo->pPkgsToDowngrade);
        BAIL_ON_TDNF_ERROR(dwError);
        if(pSolvedInfo->pPkgsRemovedByDowngrade)
        {
            dwError = TDNFTransAddErasePkgs(
                      pTS,
                      pSolvedInfo->pPkgsRemovedByDowngrade);
            BAIL_ON_TDNF_ERROR(dwError);
        }
    }

cleanup:
    return dwError;

error:
    goto cleanup;
}

uint32_t
TDNFDetectPreTransFailure(
    rpmts pTS,
    char *pszError
    )
{
    uint32_t dwError = 0;
    rpmtsi pi = NULL;
    rpmte pte = NULL;
    char *pszToken;
    int i = 0;
    char *pszErrorStr = NULL;
    char *pszPkgName = NULL;
    char *pszSymbol = NULL;
    char *pszVersion = NULL;
    char *pszCachePkgName = NULL;
    char *pszCachePkgEVR = NULL;

    if (!pTS || IsNullOrEmptyString(pszError))
    {
        dwError = ERROR_TDNF_INVALID_PARAMETER;
        BAIL_ON_TDNF_ERROR(dwError);
    }
    // Error Str has the format: <Pkg-Name> <Symbol> <version-release>
    dwError = TDNFAllocateString(pszError, &pszErrorStr);
    BAIL_ON_TDNF_ERROR(dwError);

    pszToken = strtok(pszErrorStr, " ");
    while (pszToken != NULL)
    {
        switch(i)
        {
            case 0:
                pszPkgName = pszToken;
                break;
            case 1:
                pszSymbol = pszToken;
                break;
            case 2:
                pszVersion = pszToken;
                break;
            default:
                fprintf(stderr, "RPM problem string format unsupported\n");
                dwError = ERROR_TDNF_INVALID_PARAMETER;
                BAIL_ON_TDNF_ERROR(dwError);
        }
        i++;
        pszToken = strtok(NULL, " ");
    }

    if (IsNullOrEmptyString(pszPkgName) || IsNullOrEmptyString(pszSymbol) || IsNullOrEmptyString(pszVersion))
    {
        dwError = ERROR_TDNF_INVALID_PARAMETER;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    pi = rpmtsiInit(pTS);
    while ((pte = rpmtsiNext(pi, 0)) != NULL)
    {
        dwError = TDNFAllocateString(rpmteN(pte), &pszCachePkgName);
        BAIL_ON_TDNF_ERROR(dwError);

        dwError = TDNFAllocateString(rpmteEVR(pte), &pszCachePkgEVR);
        BAIL_ON_TDNF_ERROR(dwError);

        if (strcmp(pszCachePkgName, pszPkgName) == 0)
        {
            if ((strchr(pszSymbol, '>') && (rpmvercmp(pszCachePkgEVR, pszVersion) > 0)) ||
                (strchr(pszSymbol, '<') && (rpmvercmp(pszCachePkgEVR, pszVersion) < 0)) ||
                (strchr(pszSymbol, '=') && (rpmvercmp(pszCachePkgEVR, pszVersion) == 0)))
            {
                fprintf(stderr, "Detected rpm pre-transaction dependency errors. "
                        "Install %s %s %s first to resolve this failure.\n",
                        pszPkgName, pszSymbol, pszVersion);
                break;
            }
        }
    }

cleanup:
    rpmtsiFree(pi);
    TDNF_SAFE_FREE_MEMORY(pszErrorStr);
    TDNF_SAFE_FREE_MEMORY(pszCachePkgName);
    TDNF_SAFE_FREE_MEMORY(pszCachePkgEVR);
    return dwError;
error:
    goto cleanup;
}

int
doCheck(PTDNFRPMTS pTS)
{
    int nResult = 0;
    rpmpsi psi = NULL;
    rpmProblem prob = NULL;
    nResult = rpmtsCheck(pTS->pTS);
    char *pErrorStr = NULL;
    uint32_t dwError = 0;

    rpmps ps = rpmtsProblems(pTS->pTS);
    if(ps)
    {
        int nProbs = rpmpsNumProblems(ps);
        if(nProbs > 0)
        {
            printf("Found %d problems\n", nProbs);

            psi = rpmpsInitIterator(ps);
            while(rpmpsNextIterator(psi) >= 0)
            {
                prob = rpmpsGetProblem(psi);
                char *msg = rpmProblemString(prob);
                if (strstr(msg, "no digest") != NULL)
                {
                    printf("%s. Use --skipdigest to ignore\n", msg);
                }
                else
                {
                    printf("%s\n", msg);
                    if (rpmProblemGetType(prob) == RPMPROB_REQUIRES)
                    {
                        dwError = TDNFAllocateString(rpmProblemGetStr(prob), &pErrorStr);
                        BAIL_ON_TDNF_ERROR(dwError);

                        dwError = TDNFDetectPreTransFailure(pTS->pTS, pErrorStr);
                        BAIL_ON_TDNF_ERROR(dwError);
                    }
                }
                rpmProblemFree(prob);
            }
            rpmpsFreeIterator(psi);
            nResult = ERROR_TDNF_RPM_CHECK;
        }
    }
cleanup:
    TDNF_SAFE_FREE_MEMORY(pErrorStr);
    return nResult;
error:
    goto cleanup;
}

uint32_t
TDNFRunTransaction(
    PTDNFRPMTS pTS,
    PTDNF pTdnf
    )
{
    uint32_t dwError = 0;
    int nSilent = 0;
    int rpmVfyLevelMask = 0;
    uint32_t dwSkipSignature = 0;
    uint32_t dwSkipDigest = 0;

    if(!pTS || !pTdnf || !pTdnf->pArgs)
    {
        dwError = ERROR_TDNF_INVALID_PARAMETER;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    nSilent = pTdnf->pArgs->nNoOutput;

    dwError = rpmtsOrder(pTS->pTS);
    BAIL_ON_TDNF_ERROR(dwError);

    dwError = doCheck(pTS);
    BAIL_ON_TDNF_ERROR(dwError);

    rpmtsClean(pTS->pTS);

    dwError = TDNFGetSkipSignatureOption(pTdnf, &dwSkipSignature);
    BAIL_ON_TDNF_ERROR(dwError);

    dwError = TDNFGetSkipDigestOption(pTdnf, &dwSkipDigest);
    BAIL_ON_TDNF_ERROR(dwError);

    //TODO do callbacks for output
    if(!nSilent)
    {
        printf("Testing transaction\n");
    }

    if (pTdnf->pArgs->nNoGPGCheck)
    {
        rpmtsSetVSFlags(pTS->pTS, rpmtsVSFlags(pTS->pTS) | RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES);
        rpmtsSetVfyLevel(pTS->pTS, ~RPMSIG_VERIFIABLE_TYPE);
    }

    else if (dwSkipSignature || dwSkipDigest)
    {
         if (dwSkipSignature)
         {
             rpmtsSetVSFlags(pTS->pTS, rpmtsVSFlags(pTS->pTS) | RPMVSF_MASK_NOSIGNATURES);
             rpmVfyLevelMask |= RPMSIG_SIGNATURE_TYPE;
         }
         if (dwSkipDigest)
         {
             rpmtsSetVSFlags(pTS->pTS, rpmtsVSFlags(pTS->pTS) | RPMVSF_MASK_NODIGESTS);
             rpmVfyLevelMask |= RPMSIG_DIGEST_TYPE;
         }
         rpmtsSetVfyLevel(pTS->pTS, ~rpmVfyLevelMask);
    }
    rpmtsSetFlags(pTS->pTS, RPMTRANS_FLAG_TEST);
    dwError = rpmtsRun(pTS->pTS, NULL, pTS->nProbFilterFlags);
    BAIL_ON_TDNF_RPM_ERROR(dwError);

    //TODO do callbacks for output
    if(!nSilent)
    {
        printf("Running transaction\n");
    }
    rpmtsSetFlags(pTS->pTS, RPMTRANS_FLAG_NONE);
    dwError = rpmtsRun(pTS->pTS, NULL, pTS->nProbFilterFlags);
    BAIL_ON_TDNF_RPM_ERROR(dwError);

cleanup:
    return dwError;

error:
    if(pTS && dwError != ERROR_TDNF_RPM_CHECK)
    {
        doCheck(pTS);
    }
    goto cleanup;
}

uint32_t
TDNFTransAddInstallPkgs(
    PTDNFRPMTS pTS,
    PTDNF pTdnf,
    PTDNF_PKG_INFO pInfo
    )
{
    uint32_t dwError = 0;
    if(!pInfo)
    {
        dwError = ERROR_TDNF_NO_DATA;
        BAIL_ON_TDNF_ERROR(dwError);
    }
    while(pInfo)
    {
        dwError = TDNFTransAddInstallPkg(
                      pTS,
                      pTdnf,
                      pInfo->pszLocation,
                      pInfo->pszName,
                      pInfo->pszRepoName, 0);
        pInfo = pInfo->pNext;
        BAIL_ON_TDNF_ERROR(dwError);
    }

cleanup:
    return dwError;

error:
    if(dwError == ERROR_TDNF_NO_DATA)
    {
        dwError = 0;
    }
    goto cleanup;
}

uint32_t
TDNFTransAddReInstallPkgs(
    PTDNFRPMTS pTS,
    PTDNF pTdnf,
    PTDNF_PKG_INFO pInfo
    )
{
    return TDNFTransAddInstallPkgs(pTS, pTdnf, pInfo);
}

uint32_t
TDNFFetchRemoteGPGKey(
    PTDNF pTdnf,
    const char* pszRepoName,
    const char* pszUrlGPGKey,
    char** ppszKeyLocation
    )
{
    uint32_t dwError = 0;
    char* pszFilePath = NULL;
    char* pszFilePathCopy = NULL;
    char* pszDownloadCacheDir = NULL;
    char* pszKeyLocation = NULL;

    if(!pTdnf || IsNullOrEmptyString(pszRepoName || IsNullOrEmptyString(pszUrlGPGKey)))
    {
        dwError = ERROR_TDNF_INVALID_PARAMETER;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    dwError = TDNFPathFromUri(pszUrlGPGKey, &pszKeyLocation);
    if (dwError == ERROR_TDNF_URL_INVALID)
    {
        dwError = ERROR_TDNF_KEYURL_INVALID;
    }
    BAIL_ON_TDNF_ERROR(dwError);

    dwError = TDNFAllocateStringPrintf(
                  &pszFilePath,
                  "%s/%s/keys/%s",
                  pTdnf->pConf->pszCacheDir,
                  pszRepoName,
                  pszKeyLocation);
    BAIL_ON_TDNF_ERROR(dwError);

    // dirname() may modify the contents of path, so it may be desirable to
    // pass a copy when calling this function.
    dwError = TDNFAllocateString(pszFilePath, &pszFilePathCopy);
    BAIL_ON_TDNF_ERROR(dwError);
    pszDownloadCacheDir = dirname(pszFilePathCopy);
    if(!pszDownloadCacheDir)
    {
        dwError = ENOENT;
        BAIL_ON_TDNF_SYSTEM_ERROR(dwError);
    }

    if(access(pszDownloadCacheDir, F_OK))
    {
        if(errno != ENOENT)
        {
            dwError = errno;
        }
        BAIL_ON_TDNF_SYSTEM_ERROR(dwError);

        dwError = TDNFUtilsMakeDirs(pszDownloadCacheDir);
        BAIL_ON_TDNF_ERROR(dwError);
    }

    dwError = TDNFDownloadFile(pTdnf, pszRepoName, pszUrlGPGKey, pszFilePath,
                               NULL, 0, NULL);
    BAIL_ON_TDNF_ERROR(dwError);

    *ppszKeyLocation = pszFilePath;

cleanup:
    TDNF_SAFE_FREE_MEMORY(pszFilePathCopy);
    TDNF_SAFE_FREE_MEMORY(pszKeyLocation);
    return dwError;

error:
    fprintf(stderr, "Error processing key: %s\n", pszUrlGPGKey);
    TDNF_SAFE_FREE_MEMORY(pszFilePath);
    *ppszKeyLocation = NULL;
    goto cleanup;
}

uint32_t
TDNFTransAddInstallPkg(
    PTDNFRPMTS pTS,
    PTDNF pTdnf,
    const char* pszPackageLocation,
    const char* pszPkgName,
    const char* pszRepoName,
    int nUpgrade
    )
{
    uint32_t dwError = 0;
    int nGPGCheck = 0;
    int nGPGSigCheck = 0;
    char* pszRpmCacheDir = NULL;
    char* pszFilePath = NULL;
    char* pszFilePathCopy = NULL;
    Header rpmHeader = NULL;
    FD_t fp = NULL;
    char* pszDownloadCacheDir = NULL;
    char* pszUrlGPGKey = NULL;
    char* pszLocalGPGKey = NULL;
    PTDNF_CACHED_RPM_ENTRY pRpmCache = NULL;
    rpmKeyring pKeyring = NULL;
    int nAnswer = 0;
    int nRemote = 0;

    dwError = TDNFAllocateStringPrintf(
                  &pszRpmCacheDir,
                  "%s/%s/%s",
                  pTdnf->pConf->pszCacheDir,
                  pszRepoName,
                  "rpms");
    BAIL_ON_TDNF_ERROR(dwError);

    dwError = TDNFAllocateStringPrintf(
                  &pszFilePath,
                  "%s/%s",
                  pszRpmCacheDir,
                  pszPackageLocation);
    BAIL_ON_TDNF_ERROR(dwError);

    // dirname() may modify the contents of path, so it may be desirable to
    // pass a copy when calling this function.
    dwError = TDNFAllocateString(pszFilePath, &pszFilePathCopy);
    BAIL_ON_TDNF_ERROR(dwError);
    pszDownloadCacheDir = dirname(pszFilePathCopy);
    if(!pszDownloadCacheDir)
    {
        dwError = ENOENT;
        BAIL_ON_TDNF_SYSTEM_ERROR(dwError);
    }

    if(access(pszDownloadCacheDir, F_OK))
    {
        if(errno != ENOENT)
        {
            dwError = errno;
        }
        BAIL_ON_TDNF_SYSTEM_ERROR(dwError);

        dwError = TDNFUtilsMakeDirs(pszDownloadCacheDir);
        BAIL_ON_TDNF_ERROR(dwError);
    }

    if(access(pszFilePath, F_OK))
    {
        if(errno != ENOENT)
        {
            dwError = errno;
            BAIL_ON_TDNF_SYSTEM_ERROR(dwError);
        }
        dwError = TDNFDownloadPackage(pTdnf, pszPackageLocation, pszPkgName,
            pszRepoName, pszDownloadCacheDir);
        BAIL_ON_TDNF_ERROR(dwError);
    }
    //A download could have been triggered.
    //So check access and bail if not available
    if(access(pszFilePath, F_OK))
    {
        dwError = errno;
        BAIL_ON_TDNF_SYSTEM_ERROR(dwError);
    }

    dwError = TDNFGetGPGSignatureCheck(pTdnf, pszRepoName, &nGPGSigCheck, NULL);
    BAIL_ON_TDNF_ERROR(dwError);

    fp = Fopen (pszFilePath, "r.ufdio");
    if(!fp)
    {
        dwError = errno;
        BAIL_ON_TDNF_SYSTEM_ERROR(dwError);
    }

    dwError = rpmReadPackageFile(
                  pTS->pTS,
                  fp,
                  pszFilePath,
                  &rpmHeader);

    Fclose(fp);
    fp = NULL;

    if (dwError != RPMRC_NOTTRUSTED && dwError != RPMRC_NOKEY)
    {
        BAIL_ON_TDNF_RPM_ERROR(dwError);
    }
    else if(nGPGSigCheck)
    {
        dwError = TDNFGetGPGSignatureCheck(pTdnf, pszRepoName, &nGPGSigCheck, &pszUrlGPGKey);
        BAIL_ON_TDNF_ERROR(dwError);

        printf("importing key from %s\n", pszUrlGPGKey);
        dwError = TDNFYesOrNo(pTdnf->pArgs, "Is this ok [y/N]: ", &nAnswer);
        BAIL_ON_TDNF_ERROR(dwError);

        if(!nAnswer)
        {
            dwError = ERROR_TDNF_OPERATION_ABORTED;
            BAIL_ON_TDNF_ERROR(dwError);
        }

        dwError = TDNFUriIsRemote(pszUrlGPGKey, &nRemote);
        if (dwError == ERROR_TDNF_URL_INVALID)
        {
            dwError = ERROR_TDNF_KEYURL_INVALID;
        }
        BAIL_ON_TDNF_ERROR(dwError);

        if (nRemote)
        {
            dwError = TDNFFetchRemoteGPGKey(pTdnf, pszRepoName, pszUrlGPGKey, &pszLocalGPGKey);
            BAIL_ON_TDNF_ERROR(dwError);
        }
        else
        {
            dwError = TDNFPathFromUri(pszUrlGPGKey, &pszLocalGPGKey);
            if (dwError == ERROR_TDNF_URL_INVALID)
            {
                dwError = ERROR_TDNF_KEYURL_INVALID;
            }
            BAIL_ON_TDNF_ERROR(dwError);
        }

        pKeyring = rpmtsGetKeyring(pTS->pTS, 0);

        dwError = TDNFImportGPGKey(pTS->pTS, pszLocalGPGKey);
        BAIL_ON_TDNF_ERROR(dwError);

        dwError = TDNFGPGCheck(pKeyring, pszLocalGPGKey, pszFilePath);
        BAIL_ON_TDNF_ERROR(dwError);

        if (nRemote)
        {
            if (unlink(pszLocalGPGKey))
            {
                dwError = errno;
                BAIL_ON_TDNF_SYSTEM_ERROR(dwError);
            }
        }

        fp = Fopen (pszFilePath, "r.ufdio");
        if(!fp)
        {
            dwError = errno;
            BAIL_ON_TDNF_SYSTEM_ERROR(dwError);
        }

        dwError = rpmReadPackageFile(
                      pTS->pTS,
                      fp,
                      pszFilePath,
                      &rpmHeader);

        BAIL_ON_TDNF_RPM_ERROR(dwError);

        Fclose(fp);
        fp = NULL;
    }

    dwError = TDNFGetGPGCheck(pTdnf, pszRepoName, &nGPGCheck);
    BAIL_ON_TDNF_ERROR(dwError);
    if (!nGPGCheck)
    {
        rpmtsSetVSFlags(pTS->pTS, rpmtsVSFlags(pTS->pTS) | RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES);
        rpmtsSetVfyLevel(pTS->pTS, ~RPMSIG_VERIFIABLE_TYPE);
    }

    dwError = rpmtsAddInstallElement(
                  pTS->pTS,
                  rpmHeader,
                  (fnpyKey)pszFilePath,
                  nUpgrade,
                  NULL);
    BAIL_ON_TDNF_RPM_ERROR(dwError);

    if(pTS->pCachedRpmsArray)
    {
        dwError = TDNFAllocateMemory(
                      1,
                      sizeof(TDNF_CACHED_RPM_ENTRY),
                      (void**)&pRpmCache);
        BAIL_ON_TDNF_ERROR(dwError);
        pRpmCache->pszFilePath = pszFilePath;
        pRpmCache->pNext = pTS->pCachedRpmsArray->pHead;
        pTS->pCachedRpmsArray->pHead = pRpmCache;
    }
cleanup:
    if(pKeyring)
    {
        rpmKeyringFree(pKeyring);
    }
    TDNF_SAFE_FREE_MEMORY(pszFilePathCopy);
    TDNF_SAFE_FREE_MEMORY(pszUrlGPGKey);
    TDNF_SAFE_FREE_MEMORY(pszLocalGPGKey);
    TDNF_SAFE_FREE_MEMORY(pszRpmCacheDir);
    if(fp)
    {
        Fclose(fp);
    }
    if(rpmHeader)
    {
        headerFree(rpmHeader);
    }
    if(!pTS->pCachedRpmsArray && dwError == 0)
    {
        TDNF_SAFE_FREE_MEMORY(pszFilePath);
    }
    return dwError;

error:
    fprintf(stderr, "Error processing package: %s\n", pszPackageLocation);
    TDNF_SAFE_FREE_MEMORY(pszFilePath);
    TDNF_SAFE_FREE_MEMORY(pRpmCache);
    goto cleanup;
}

uint32_t
TDNFTransAddUpgradePkgs(
    PTDNFRPMTS pTS,
    PTDNF pTdnf,
    PTDNF_PKG_INFO pInfo)
{
    uint32_t dwError = 0;
    if(!pInfo)
    {
        dwError = ERROR_TDNF_NO_DATA;
        BAIL_ON_TDNF_ERROR(dwError);
    }
    while(pInfo)
    {
        dwError = TDNFTransAddInstallPkg(
                      pTS,
                      pTdnf,
                      pInfo->pszLocation,
                      pInfo->pszName,
                      pInfo->pszRepoName,
                      1);
        pInfo = pInfo->pNext;
        BAIL_ON_TDNF_ERROR(dwError);
    }

cleanup:
    return dwError;

error:
    if(dwError == ERROR_TDNF_NO_DATA)
    {
        dwError = 0;
    }
    goto cleanup;
}

uint32_t
TDNFTransAddObsoletedPkgs(
    PTDNFRPMTS pTS,
    PTDNF_PKG_INFO pInfo
    )
{
    uint32_t dwError = 0;

    if(!pInfo)
    {
        dwError = ERROR_TDNF_NO_DATA;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    while(pInfo)
    {
        dwError = TDNFTransAddErasePkg(pTS, pInfo->pszName);
        pInfo = pInfo->pNext;
        BAIL_ON_TDNF_ERROR(dwError);
    }

cleanup:
    return dwError;

error:
    if(dwError == ERROR_TDNF_NO_DATA)
    {
        dwError = 0;
    }
    goto cleanup;
}

uint32_t
TDNFTransAddErasePkg(
    PTDNFRPMTS pTS,
    const char* pkgName
    )
{
    uint32_t dwError = 0;
    Header pRpmHeader = NULL;
    rpmdbMatchIterator pIterator = NULL;
    unsigned int nOffset = 0;

    if(!pTS || IsNullOrEmptyString(pkgName))
    {
        dwError = ERROR_TDNF_INVALID_PARAMETER;
        BAIL_ON_TDNF_ERROR(dwError);
    }

    pIterator = rpmtsInitIterator(pTS->pTS, (rpmTag)RPMDBI_LABEL, pkgName, 0);
    while ((pRpmHeader = rpmdbNextIterator(pIterator)) != NULL)
    {
        nOffset = rpmdbGetIteratorOffset(pIterator);
        if(nOffset)
        {
            dwError = rpmtsAddEraseElement(pTS->pTS, pRpmHeader, nOffset);
            BAIL_ON_TDNF_ERROR(dwError);
        }
    }

cleanup:
    if(pIterator)
    {
        rpmdbFreeIterator(pIterator);
    }
    return dwError;

error:
    goto cleanup;
}

void*
TDNFRpmCB(
     const void* pArg,
     const rpmCallbackType what,
     const rpm_loff_t amount,
     const rpm_loff_t total,
     fnpyKey key,
     rpmCallbackData data
     )
{
    Header pPkgHeader = (Header) pArg;
    void* pResult = NULL;
    char* pszFileName = (char*)key;
    PTDNFRPMTS pTS = (PTDNFRPMTS)data;

    UNUSED(total);
    UNUSED(amount);

    switch (what)
    {
        case RPMCALLBACK_INST_OPEN_FILE:
            if(IsNullOrEmptyString(pszFileName))
            {
                return NULL;
            }
            pTS->pFD = Fopen(pszFileName, "r.ufdio");
            return (void *)pTS->pFD;
            break;

        case RPMCALLBACK_INST_CLOSE_FILE:
            if(pTS->pFD)
            {
                Fclose(pTS->pFD);
                pTS->pFD = NULL;
            }
            break;
        case RPMCALLBACK_INST_START:
        case RPMCALLBACK_UNINST_START:
            if(pTS->nQuiet)
                break;
            if(what == RPMCALLBACK_INST_START)
            {
                printf("%s", "Installing/Updating: ");
            }
            else
            {
                printf("%s", "Removing: ");
            }
            {
                char* pszNevra = headerGetAsString(pPkgHeader, RPMTAG_NEVRA);
                printf("%s\n", pszNevra);
                free(pszNevra);
                (void)fflush(stdout);
            }
            break;
        default:
            break;
    }

    return pResult;
}

uint32_t
TDNFRemoveCachedRpms(
    PTDNF_CACHED_RPM_LIST pCachedRpmsList
    )
{
    uint32_t dwError = 0;
    PTDNF_CACHED_RPM_ENTRY pCur = NULL;

    if(!pCachedRpmsList)
    {
        dwError = ERROR_TDNF_INVALID_PARAMETER;
        BAIL_ON_TDNF_ERROR(dwError);
    }
    pCur = pCachedRpmsList->pHead;
    while(pCur != NULL)
    {
        if(!IsNullOrEmptyString(pCur->pszFilePath))
        {
            if(unlink(pCur->pszFilePath))
            {
                dwError = errno;
                BAIL_ON_TDNF_SYSTEM_ERROR(dwError);
            }
        }
        pCur = pCur->pNext;
    }

cleanup:
    return dwError;

error:
    goto cleanup;
    return 1;
}

void
TDNFFreeCachedRpmsArray(
    PTDNF_CACHED_RPM_LIST pArray
    )
{
    PTDNF_CACHED_RPM_ENTRY pCur = NULL;
    PTDNF_CACHED_RPM_ENTRY pNext = NULL;

    if(pArray)
    {
        pCur = pArray->pHead;
        while(pCur != NULL)
        {
            pNext = pCur->pNext;
            TDNF_SAFE_FREE_MEMORY(pCur->pszFilePath);
            TDNF_SAFE_FREE_MEMORY(pCur);
            pCur = pNext;
        }
        TDNF_SAFE_FREE_MEMORY(pArray);
    }
}
