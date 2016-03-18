/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TorFileUtils_h__
#define TorFileUtils_h__

#include "nsIFile.h"

class nsIFile;

/**
 * TorBrowser_GetUserDataDir
 *
 * Retrieve the Tor Browser user data directory.
 * When built with --enable-tor-browser-data-outside-app-dir, the directory
 * is next to the application directory, except on Mac OS where it may be
 * there or it may be at ~/Library/Application Support/TorBrowser-Data (the
 * latter location is used if the .app bundle is in a directory whose path
 * contains /Applications or if we lack write access to the directory that
 * contains the .app).
 * When built without --enable-tor-browser-data-outside-app-dir, this
 * directory is TorBrowser.app/TorBrowser.
 *
 * @param aExeFile  The firefox executable.
 * @param aFile     Out parameter that is set to the Tor Browser user data
 *                  directory.
 * @return NS_OK on success.  Error otherwise.
 */
extern NS_METHOD
TorBrowser_GetUserDataDir(nsIFile *aExeFile, nsIFile** aFile);

#endif // !TorFileUtils_h__
