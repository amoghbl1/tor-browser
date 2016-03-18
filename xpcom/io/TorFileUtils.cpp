/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TorFileUtils.h"

static nsresult GetAppRootDir(nsIFile *aExeFile, nsIFile** aFile);

//-----------------------------------------------------------------------------
NS_METHOD
TorBrowser_GetUserDataDir(nsIFile *aExeFile, nsIFile** aFile)
{
  NS_ENSURE_ARG_POINTER(aFile);
  nsCOMPtr<nsIFile> tbDataDir;

#ifdef TOR_BROWSER_DATA_OUTSIDE_APP_DIR
  nsAutoCString tbDataLeafName(NS_LITERAL_CSTRING("TorBrowser-Data"));
  nsCOMPtr<nsIFile> appRootDir;
  nsresult rv = GetAppRootDir(aExeFile, getter_AddRefs(appRootDir));
  NS_ENSURE_SUCCESS(rv, rv);
#ifndef XP_MACOSX
  // On all platforms except Mac OS, we always operate in a "portable" mode
  // where the TorBrowser-Data directory is located next to the application.
  rv = appRootDir->GetParent(getter_AddRefs(tbDataDir));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = tbDataDir->AppendNative(tbDataLeafName);
  NS_ENSURE_SUCCESS(rv, rv);
#else
  // For Mac OS, determine whether we should store user data in the OS's
  // standard location (i.e., under ~/Library/Application Support). We use
  // the OS location if (1) the application is installed in a directory whose
  // path contains "/Applications" or (2) the TorBrowser-Data directory does
  // not exist and cannot be created (which probably means we lack write
  // permission to the directory that contains the application).
  nsAutoString appRootPath;
  rv = appRootDir->GetPath(appRootPath);
  NS_ENSURE_SUCCESS(rv, rv);
  bool useOSLocation = (appRootPath.Find("/Applications",
                                         true /* ignore case */) >= 0);
  if (!useOSLocation) {
    // We hope to use the portable (aka side-by-side) approach, but before we
    // commit to that, let's ensure that we can create the TorBrowser-Data
    // directory. If it already exists, we will try to use it; if not and we
    // fail to create it, we will switch to ~/Library/Application Support.
    rv = appRootDir->GetParent(getter_AddRefs(tbDataDir));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = tbDataDir->AppendNative(tbDataLeafName);
    NS_ENSURE_SUCCESS(rv, rv);
    bool exists = false;
    rv = tbDataDir->Exists(&exists);
    if (NS_SUCCEEDED(rv) && !exists)
      rv = tbDataDir->Create(nsIFile::DIRECTORY_TYPE, 0700);
    useOSLocation = NS_FAILED(rv);
  }

  if (useOSLocation) {
    // We are using ~/Library/Application Support/TorBrowser-Data. We do not
    // need to create that directory here because the code in nsXREDirProvider
    // will do so (and the user should always have write permission for
    // ~/Library/Application Support; if they do not we have no more options).
    FSRef fsRef;
    OSErr err = ::FSFindFolder(kUserDomain, kApplicationSupportFolderType,
                               kCreateFolder, &fsRef);
    NS_ENSURE_FALSE(err, NS_ERROR_FAILURE);
    // To convert the FSRef returned by FSFindFolder() into an nsIFile that
    // points to ~/Library/Application Support, we first create an empty
    // nsIFile object (no path) and then use InitWithFSRef() to set the
    // path.
    rv = NS_NewNativeLocalFile(EmptyCString(), true,
                               getter_AddRefs(tbDataDir));
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<nsILocalFileMac> dirFileMac = do_QueryInterface(tbDataDir);
    if (!dirFileMac)
      return NS_ERROR_UNEXPECTED;
    rv = dirFileMac->InitWithFSRef(&fsRef);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = tbDataDir->AppendNative(tbDataLeafName);
    NS_ENSURE_SUCCESS(rv, rv);
  }
#endif

#elif defined(ANDROID)
  // Orfox stores data in the app home directory.
  const char* homeDir = getenv("HOME");
  if (!homeDir || !*homeDir)
    return NS_ERROR_FAILURE;
  nsresult rv = NS_NewNativeLocalFile(nsDependentCString(homeDir), true,
                                      getter_AddRefs(tbDataDir));
#else
  // User data is embedded within the application directory (i.e.,
  // TOR_BROWSER_DATA_OUTSIDE_APP_DIR is not defined).
  nsresult rv = GetAppRootDir(aExeFile, getter_AddRefs(tbDataDir));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = tbDataDir->AppendNative(NS_LITERAL_CSTRING("TorBrowser"));
  NS_ENSURE_SUCCESS(rv, rv);
#endif

  tbDataDir.forget(aFile);
  return NS_OK;
}

static nsresult
GetAppRootDir(nsIFile *aExeFile, nsIFile** aFile)
{
  NS_ENSURE_ARG_POINTER(aExeFile);
  NS_ENSURE_ARG_POINTER(aFile);
  nsCOMPtr<nsIFile> appRootDir = aExeFile;

  int levelsToRemove = 1; // Remove firefox (the executable file).
#if defined(XP_MACOSX)
  levelsToRemove += 2;   // On Mac OS, we must also remove Contents/MacOS.
#endif
  while (appRootDir && (levelsToRemove > 0)) {
    // When crawling up the hierarchy, components named "." do not count.
    nsAutoCString removedName;
    nsresult rv = appRootDir->GetNativeLeafName(removedName);
    NS_ENSURE_SUCCESS(rv, rv);
    bool didRemove = !removedName.Equals(".");

    // Remove a directory component.
    nsCOMPtr<nsIFile> parentDir;
    rv = appRootDir->GetParent(getter_AddRefs(parentDir));
    NS_ENSURE_SUCCESS(rv, rv);
    appRootDir = parentDir;

    if (didRemove)
      --levelsToRemove;
  }

  if (!appRootDir)
    return NS_ERROR_FAILURE;

  appRootDir.forget(aFile);
  return NS_OK;
}
