/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: NPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is 
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or 
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the NPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the NPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef nsJSNPRuntime_h__
#define nsJSNPRuntime_h__

#include "nscore.h"
#include "jsapi.h"
#include "npapi.h"
#include "npruntime.h"
#include "pldhash.h"

class nsJSNPRuntime
{
public:
  static void OnPluginDestroy(NPP npp);
};

class nsJSObjWrapper : public NPObject
{
public:
  static NPObject *GetNewOrUsed(NPP npp, JSContext *cx, JSObject *obj);

protected:
  nsJSObjWrapper();
  ~nsJSObjWrapper();

  static NPObject * NP_Allocate();
  static void NP_Deallocate(NPObject *obj);
  static void NP_Invalidate(NPObject *obj);
  static bool NP_HasMethod(NPObject *, NPIdentifier identifier);
  static bool NP_Invoke(NPObject *obj, NPIdentifier method,
                        const NPVariant *args, uint32_t argCount,
                        NPVariant *result);
  static bool NP_HasProperty(NPObject * obj, NPIdentifier property);
  static bool NP_GetProperty(NPObject *obj, NPIdentifier property,
                             NPVariant *result);
  static bool NP_SetProperty(NPObject *obj, NPIdentifier property,
                             const NPVariant *value);
  static bool NP_RemoveProperty(NPObject *obj, NPIdentifier property);

public:
  JSObject *mJSObj;
  JSContext *mCx;
  NPP mNpp;

  static NPClass sJSObjWrapperNPClass;
};

class nsNPObjWrapper
{
public:
  static JSObject *GetNewOrUsed(NPP npp, JSContext *cx, NPObject *npobj);
};

bool
JSValToNPVariant(NPP npp, JSContext *cx, jsval val, NPVariant *variant);

#endif // nsJSNPRuntime_h__
