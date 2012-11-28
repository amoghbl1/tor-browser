/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ThirdPartyUtil.h"
#include "mozilla/Preferences.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsIChannel.h"
#include "nsIServiceManager.h"
#include "nsIHttpChannelInternal.h"
#include "nsIDOMWindow.h"
#include "nsICookiePermission.h"
#include "nsIDOMDocument.h"
#include "nsIDocument.h"
#include "nsILoadContext.h"
#include "nsIPrincipal.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsThreadUtils.h"
#include "mozilla/Logging.h"
#include "nsPrintfCString.h"
#include "nsIConsoleService.h"
#include "nsContentUtils.h"
#include "nsIContent.h"

NS_IMPL_ISUPPORTS(ThirdPartyUtil, mozIThirdPartyUtil)

//
// NSPR_LOG_MODULES=thirdPartyUtil:5
//
static mozilla::LazyLogModule gThirdPartyLog("thirdPartyUtil");
#undef LOG
#define LOG(args)     MOZ_LOG(gThirdPartyLog, mozilla::LogLevel::Debug, args)

// static
mozIThirdPartyUtil* ThirdPartyUtil::gThirdPartyUtilService = nullptr;

//static
nsresult
ThirdPartyUtil::GetFirstPartyHost(nsIChannel* aChannel, nsINode* aNode, nsACString& aResult)
{
  if (!gThirdPartyUtilService) {
    nsresult rv = CallGetService(THIRDPARTYUTIL_CONTRACTID, &gThirdPartyUtilService);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  nsCOMPtr<nsIURI> isolationURI;
  nsresult rv = gThirdPartyUtilService->GetFirstPartyIsolationURI(aChannel, aNode, getter_AddRefs(isolationURI));
  NS_ENSURE_SUCCESS(rv, rv);
  if (!isolationURI) {
    // Isolation is not active.
    aResult.Truncate();
    return NS_OK;
  }
  return gThirdPartyUtilService->GetFirstPartyHostForIsolation(isolationURI, aResult);
}

nsresult
ThirdPartyUtil::Init()
{
  NS_ENSURE_TRUE(NS_IsMainThread(), NS_ERROR_NOT_AVAILABLE);

  nsresult rv;
  mTLDService = do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID, &rv);
  mCookiePermissions = do_GetService(NS_COOKIEPERMISSION_CONTRACTID);

  return rv;
}

// Determine if aFirstDomain is a different base domain to aSecondURI; or, if
// the concept of base domain does not apply, determine if the two hosts are not
// string-identical.
nsresult
ThirdPartyUtil::IsThirdPartyInternal(const nsCString& aFirstDomain,
                                     nsIURI* aSecondURI,
                                     bool* aResult)
{
  if (!aSecondURI) {
    return NS_ERROR_INVALID_ARG;
  }

  // Get the base domain for aSecondURI.
  nsCString secondDomain;
  nsresult rv = GetBaseDomain(aSecondURI, secondDomain);
  LOG(("ThirdPartyUtil::IsThirdPartyInternal %s =? %s", aFirstDomain.get(), secondDomain.get()));
  if (NS_FAILED(rv))
    return rv;

  // Check strict equality.
  *aResult = aFirstDomain != secondDomain;
  return NS_OK;
}

// Return true if aURI's scheme is white listed, in which case
// getFirstPartyURI() will not require that the firstPartyURI contains a host.
bool ThirdPartyUtil::SchemeIsWhiteListed(nsIURI *aURI)
{
  if (!aURI)
    return false;

  nsAutoCString scheme;
  nsresult rv = aURI->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, false);

  return (scheme.Equals("about") || scheme.Equals("moz-safe-about")
          || scheme.Equals("chrome"));
}

// Get the URI associated with a window.
NS_IMETHODIMP
ThirdPartyUtil::GetURIFromWindow(nsIDOMWindow* aWin, nsIURI** result)
{
  nsresult rv;
  nsCOMPtr<nsIScriptObjectPrincipal> scriptObjPrin = do_QueryInterface(aWin);
  if (!scriptObjPrin) {
    return NS_ERROR_INVALID_ARG;
  }

  nsIPrincipal* prin = scriptObjPrin->GetPrincipal();
  if (!prin) {
    return NS_ERROR_INVALID_ARG;
  }

  if (prin->GetIsNullPrincipal()) {
    LOG(("ThirdPartyUtil::GetURIFromWindow can't use null principal\n"));
    return NS_ERROR_INVALID_ARG;
  }

  rv = prin->GetURI(result);
  return rv;
}

nsresult
ThirdPartyUtil::GetOriginatingURI(nsIChannel *aChannel, nsIURI **aURI)
{
  /* to find the originating URI, we use the loadgroup of the channel to obtain
   * the window owning the load, and from there, we find the top same-type
   * window and its URI. there are several possible cases:
   *
   * 1) no channel.
   *
   * 2) a channel with the "force allow third party cookies" option set.
   *    since we may not have a window, we return the channel URI in this case.
   *
   * 3) a channel, but no window. this can occur when the consumer kicking
   *    off the load doesn't provide one to the channel, and should be limited
   *    to loads of certain types of resources.
   *
   * 4) a window equal to the top window of same type, with the channel its
   *    document channel. this covers the case of a freshly kicked-off load
   *    (e.g. the user typing something in the location bar, or clicking on a
   *    bookmark), where the window's URI hasn't yet been set, and will be
   *    bogus. we return the channel URI in this case.
   *
   * 5) Anything else. this covers most cases for an ordinary page load from
   *    the location bar, and will catch nested frames within a page, image
   *    loads, etc. we return the URI of the root window's document's principal
   *    in this case.
   */

  *aURI = nullptr;

  // case 1)
  if (!aChannel)
    return NS_ERROR_NULL_POINTER;

  // case 2)
  nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal = do_QueryInterface(aChannel);
  if (httpChannelInternal)
  {
    bool doForce = false;
    if (NS_SUCCEEDED(httpChannelInternal->GetForceAllowThirdPartyCookie(&doForce)) && doForce)
    {
      // return the channel's URI (we may not have a window)
      aChannel->GetURI(aURI);
      if (!*aURI)
        return NS_ERROR_NULL_POINTER;

      return NS_OK;
    }

    // TODO: Why don't we just use this here:
    // httpChannelInternal->GetDocumentURI(aURI);
  }

  // find the associated window and its top window
  nsCOMPtr<nsILoadContext> ctx;
  NS_QueryNotificationCallbacks(aChannel, ctx);
  nsCOMPtr<nsIDOMWindow> topWin, ourWin;
  if (ctx) {
    ctx->GetTopWindow(getter_AddRefs(topWin));
    ctx->GetAssociatedWindow(getter_AddRefs(ourWin));
  }

  // case 3)
  if (!topWin || nsContentUtils::IsChromeWindow(topWin))
  {
    if (httpChannelInternal)
    {
      httpChannelInternal->GetDocumentURI(aURI);
      if (*aURI) {
        return NS_OK;
      }
    }
    return NS_ERROR_INVALID_ARG;
  }

  // case 4)
  if (ourWin == topWin) {
    // Check whether this is the document channel for this window (representing
    // a load of a new page).  This is a bit of a nasty hack, but we will
    // hopefully flag these channels better later.
    nsLoadFlags flags;
    aChannel->GetLoadFlags(&flags);

    if (flags & nsIChannel::LOAD_DOCUMENT_URI) {
      // get the channel URI - the window's will be bogus
      aChannel->GetURI(aURI);
      if (!*aURI)
        return NS_ERROR_NULL_POINTER;

      return NS_OK;
    }
  }

  // case 5) - get the originating URI from the top window's principal
  nsCOMPtr<nsIScriptObjectPrincipal> scriptObjPrin = do_QueryInterface(topWin);
  NS_ENSURE_TRUE(scriptObjPrin, NS_ERROR_UNEXPECTED);

  nsIPrincipal* prin = scriptObjPrin->GetPrincipal();
  NS_ENSURE_TRUE(prin, NS_ERROR_UNEXPECTED);

  prin->GetURI(aURI);

  if (!*aURI)
    return NS_ERROR_NULL_POINTER;

  // all done!
  return NS_OK;
}

// Determine if aFirstURI is third party with respect to aSecondURI. See docs
// for mozIThirdPartyUtil.
NS_IMETHODIMP
ThirdPartyUtil::IsThirdPartyURI(nsIURI* aFirstURI,
                                nsIURI* aSecondURI,
                                bool* aResult)
{
  NS_ENSURE_ARG(aFirstURI);
  NS_ENSURE_ARG(aSecondURI);
  NS_ASSERTION(aResult, "null outparam pointer");

  nsCString firstHost;
  nsresult rv = GetBaseDomain(aFirstURI, firstHost);
  if (NS_FAILED(rv))
    return rv;

  return IsThirdPartyInternal(firstHost, aSecondURI, aResult);
}

// Determine if any URI of the window hierarchy of aWindow is foreign with
// respect to aSecondURI. See docs for mozIThirdPartyUtil.
NS_IMETHODIMP
ThirdPartyUtil::IsThirdPartyWindow(nsIDOMWindow* aWindow,
                                   nsIURI* aURI,
                                   bool* aResult)
{
  NS_ENSURE_ARG(aWindow);
  NS_ASSERTION(aResult, "null outparam pointer");

  bool result;

  // Get the URI of the window, and its base domain.
  nsresult rv;
  nsCOMPtr<nsIURI> currentURI;
  rv = GetURIFromWindow(aWindow, getter_AddRefs(currentURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString bottomDomain;
  rv = GetBaseDomain(currentURI, bottomDomain);
  if (NS_FAILED(rv))
    return rv;

  if (aURI) {
    // Determine whether aURI is foreign with respect to currentURI.
    rv = IsThirdPartyInternal(bottomDomain, aURI, &result);
    if (NS_FAILED(rv))
      return rv;

    if (result) {
      *aResult = true;
      return NS_OK;
    }
  }

  nsCOMPtr<nsPIDOMWindow> current = do_QueryInterface(aWindow), parent;
  nsCOMPtr<nsIURI> parentURI;
  do {
    // We use GetScriptableParent rather than GetParent because we consider
    // <iframe mozbrowser/mozapp> to be a top-level frame.
    parent = current->GetScriptableParent();
    if (SameCOMIdentity(parent, current)) {
      // We're at the topmost content window. We already know the answer.
      *aResult = false;
      return NS_OK;
    }

    rv = GetURIFromWindow(parent, getter_AddRefs(parentURI));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = IsThirdPartyInternal(bottomDomain, parentURI, &result);
    if (NS_FAILED(rv))
      return rv;

    if (result) {
      *aResult = true;
      return NS_OK;
    }

    current = parent;
    currentURI = parentURI;
  } while (1);

  NS_NOTREACHED("should've returned");
  return NS_ERROR_UNEXPECTED;
}

// Determine if the URI associated with aChannel or any URI of the window
// hierarchy associated with the channel is foreign with respect to aSecondURI.
// See docs for mozIThirdPartyUtil.
NS_IMETHODIMP 
ThirdPartyUtil::IsThirdPartyChannel(nsIChannel* aChannel,
                                    nsIURI* aURI,
                                    bool* aResult)
{
  LOG(("ThirdPartyUtil::IsThirdPartyChannel [channel=%p]", aChannel));
  NS_ENSURE_ARG(aChannel);
  NS_ASSERTION(aResult, "null outparam pointer");

  nsresult rv;
  bool doForce = false;
  nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
    do_QueryInterface(aChannel);
  if (httpChannelInternal) {
    uint32_t flags;
    rv = httpChannelInternal->GetThirdPartyFlags(&flags);
    NS_ENSURE_SUCCESS(rv, rv);

    doForce = (flags & nsIHttpChannelInternal::THIRD_PARTY_FORCE_ALLOW);

    // If aURI was not supplied, and we're forcing, then we're by definition
    // not foreign. If aURI was supplied, we still want to check whether it's
    // foreign with respect to the channel URI. (The forcing only applies to
    // whatever window hierarchy exists above the channel.)
    if (doForce && !aURI) {
      *aResult = false;
      return NS_OK;
    }
  }

  bool parentIsThird = false;

  // Obtain the URI from the channel, and its base domain.
  nsCOMPtr<nsIURI> channelURI;
  rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(channelURI));
  if (NS_FAILED(rv))
    return rv;

  nsCString channelDomain;
  rv = GetBaseDomain(channelURI, channelDomain);
  if (NS_FAILED(rv))
    return rv;

  if (!doForce) {
    if (nsCOMPtr<nsILoadInfo> loadInfo = aChannel->GetLoadInfo()) {
      parentIsThird = loadInfo->GetIsInThirdPartyContext();
      if (!parentIsThird &&
          loadInfo->GetExternalContentPolicyType() != nsIContentPolicy::TYPE_DOCUMENT) {
        // Check if the channel itself is third-party to its own requestor.
        // Unforunately, we have to go through the loading principal.
        nsCOMPtr<nsIURI> parentURI;
        loadInfo->LoadingPrincipal()->GetURI(getter_AddRefs(parentURI));
        rv = IsThirdPartyInternal(channelDomain, parentURI, &parentIsThird);
        if (NS_FAILED(rv))
          return rv;
      }
    } else {
      NS_WARNING("Found channel with no loadinfo, assuming third-party request");
      parentIsThird = true;
    }
  }

  // If we're not comparing to a URI, we have our answer. Otherwise, if
  // parentIsThird, we're not forcing and we know that we're a third-party
  // request.
  if (!aURI || parentIsThird) {
    *aResult = parentIsThird;
    return NS_OK;
  }

  // Determine whether aURI is foreign with respect to channelURI.
  return IsThirdPartyInternal(channelDomain, aURI, aResult);
}

NS_IMETHODIMP
ThirdPartyUtil::GetTopWindowForChannel(nsIChannel* aChannel, nsIDOMWindow** aWin)
{
  NS_ENSURE_ARG(aWin);

  // Find the associated window and its parent window.
  nsCOMPtr<nsILoadContext> ctx;
  NS_QueryNotificationCallbacks(aChannel, ctx);
  if (!ctx) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIDOMWindow> window;
  ctx->GetAssociatedWindow(getter_AddRefs(window));
  nsCOMPtr<nsPIDOMWindow> top = do_QueryInterface(window);
  if (!top) {
    return NS_ERROR_INVALID_ARG;
  }
  
  top = top->GetTop();
  top.forget(aWin);
  return NS_OK;
}

// Get the base domain for aHostURI; e.g. for "www.bbc.co.uk", this would be
// "bbc.co.uk". Only properly-formed URI's are tolerated, though a trailing
// dot may be present. If aHostURI is an IP address, an alias such as
// 'localhost', an eTLD such as 'co.uk', or the empty string, aBaseDomain will
// be the exact host. The result of this function should only be used in exact
// string comparisons, since substring comparisons will not be valid for the
// special cases elided above.
NS_IMETHODIMP
ThirdPartyUtil::GetBaseDomain(nsIURI* aHostURI,
                              nsACString& aBaseDomain)
{
  if (!aHostURI) {
    return NS_ERROR_INVALID_ARG;
  }

  // Get the base domain. this will fail if the host contains a leading dot,
  // more than one trailing dot, or is otherwise malformed.
  nsresult rv = mTLDService->GetBaseDomain(aHostURI, 0, aBaseDomain);
  if (rv == NS_ERROR_HOST_IS_IP_ADDRESS ||
      rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
    // aHostURI is either an IP address, an alias such as 'localhost', an eTLD
    // such as 'co.uk', or the empty string. Uses the normalized host in such
    // cases.
    rv = aHostURI->GetAsciiHost(aBaseDomain);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // aHostURI (and thus aBaseDomain) may be the string '.'. If so, fail.
  if (aBaseDomain.Length() == 1 && aBaseDomain.Last() == '.')
    return NS_ERROR_INVALID_ARG;

  // Reject any URIs without a host that aren't file:// URIs. This makes it the
  // only way we can get a base domain consisting of the empty string, which
  // means we can safely perform foreign tests on such URIs where "not foreign"
  // means "the involved URIs are all file://".
  if (aBaseDomain.IsEmpty()) {
    bool isFileURI = false;
    aHostURI->SchemeIs("file", &isFileURI);
    if (!isFileURI) {
     return NS_ERROR_INVALID_ARG;
    }
  }

  return NS_OK;
}

// Determine if First Party Isolation is currently active for the given
// nsIChannel or nsIDocument.  Depends on preference setting and
// possibly the state of Private Browsing mode.
NS_IMETHODIMP
ThirdPartyUtil::IsFirstPartyIsolationActive(nsIChannel *aChannel,
                                            nsIDocument *aDoc,
                                            bool* aResult)
{
  NS_ASSERTION(aResult, "null outparam pointer");

  int32_t isolationState = mozilla::Preferences::GetInt("privacy.thirdparty.isolate");
  if (isolationState == 1) {
    if (!aChannel && aDoc) {
      // No channel passed directly. Can we get a channel from aDoc?
      aChannel = aDoc->GetChannel();
    }
    *aResult = aChannel && NS_UsePrivateBrowsing(aChannel);
  } else { // (isolationState == 0) || (isolationState == 2)
    *aResult = (isolationState == 2);
  }

  return NS_OK;
}

// Produces a URI that uniquely identifies the first party to which
// image cache and dom storage objects should be isolated. If isolation
// is deactivated, then aOutput will return null.
// Not scriptable due to the use of an nsIDocument parameter.
NS_IMETHODIMP
ThirdPartyUtil::GetFirstPartyIsolationURI(nsIChannel *aChannel, nsINode *aNode, nsIURI **aOutput)
{
  nsCOMPtr<nsIDocument> aDoc(aNode ? aNode->GetCurrentDoc() : nullptr);
  bool isolationActive = false;
  (void)IsFirstPartyIsolationActive(aChannel, aDoc, &isolationActive);
  if (isolationActive) {
    return GetFirstPartyURI(aChannel, aNode, aOutput);
  } else {
    // We return a null pointer when isolation is off.
    *aOutput = nullptr;
    return NS_OK;
  }
}

// Not scriptable due to the use of an nsIDocument parameter.
NS_IMETHODIMP
ThirdPartyUtil::GetFirstPartyURI(nsIChannel *aChannel,
                                 nsINode *aNode,
                                 nsIURI **aOutput)
{
  return GetFirstPartyURIInternal(aChannel, aNode, true, aOutput);
}

nsresult
ThirdPartyUtil::GetFirstPartyURIInternal(nsIChannel *aChannel,
                                         nsINode *aNode,
                                         bool aLogErrors,
                                         nsIURI **aOutput)
{
  nsresult rv = NS_ERROR_NULL_POINTER;
  nsCOMPtr<nsIURI> srcURI;

  if (!aOutput)
    return rv;

  *aOutput = nullptr;

  // Favicons, or other items being loaded in chrome that belong
  // to a particular web site should be assigned that site's first party.
  // If the originating node has a "firstparty" attribute
  // containing a URI string, then it is returned.
  nsCOMPtr<nsINode> node = aNode;
  if (!node && aChannel) {
    if (nsCOMPtr<nsILoadInfo> loadInfo = aChannel->GetLoadInfo()) {
      node = loadInfo->LoadingNode();
    }
  }
  if (node && node->IsElement() && node->OwnerDoc() &&
      nsContentUtils::IsChromeDoc(node->OwnerDoc())) {
    nsString firstparty;
    node->AsElement()->GetAttribute(NS_LITERAL_STRING("firstparty"), firstparty);
    if (!firstparty.IsEmpty()) {
      nsCOMPtr<nsIURI> tempURI;
      rv = NS_NewURI(getter_AddRefs(tempURI), firstparty);
      if (rv != NS_OK) {
        return rv;
      } else {
        NS_ADDREF(*aOutput = tempURI);
        return NS_OK;
      }
    }
  }

  nsCOMPtr<nsIDocument> aDoc(aNode ? aNode->GetCurrentDoc() : nullptr);

  if (!aChannel && aDoc) {
    aChannel = aDoc->GetChannel();
  }

  // If aChannel is specified or available, use the official route
  // for sure
  if (aChannel) {
    rv = GetOriginatingURI(aChannel, aOutput);
    aChannel->GetURI(getter_AddRefs(srcURI));
    if (NS_SUCCEEDED(rv) && *aOutput) {
      // At this point, about: and chrome: URLs have been mapped to file: or
      // jar: URLs.  Try to recover the original URL.
      nsAutoCString scheme;
      nsresult rv2 = (*aOutput)->GetScheme(scheme);
      NS_ENSURE_SUCCESS(rv2, rv2);
      if (scheme.Equals("file") || scheme.Equals("jar")) {
        nsCOMPtr<nsIURI> originalURI;
        rv2 = aChannel->GetOriginalURI(getter_AddRefs(originalURI));
        if (NS_SUCCEEDED(rv2) && originalURI) {
          NS_RELEASE(*aOutput);
          NS_ADDREF(*aOutput = originalURI);
        }
      }
    }
  }

  // If the channel was missing, closed or broken, try the
  // window hierarchy directly.
  //
  // This might fail to work for first-party loads themselves, but
  // we don't need this codepath for that case.
  if (NS_FAILED(rv) && aDoc) {
    nsCOMPtr<nsPIDOMWindow> top;
    nsCOMPtr<nsIDOMDocument> topDDoc;
    nsIURI *docURI = nullptr;
    srcURI = aDoc->GetDocumentURI();

    if (aDoc->GetWindow()) {
      top = aDoc->GetWindow()->GetTop();
      if (top) {
        nsCOMPtr<nsIDocument> topDoc = top->GetExtantDoc();
        if (topDoc) {
          docURI = topDoc->GetOriginalURI();
          if (docURI) {
            // Give us a mutable URI and also addref
            rv = NS_EnsureSafeToReturn(docURI, aOutput);
          }
        }
      }
    } else {
      // XXX: Chrome callers (such as NoScript) can end up here
      // through getImageData/canvas usage with no document state
      // (no Window and a document URI of about:blank). Propogate
      // rv fail (by doing nothing), and hope caller recovers.
    }

    if (*aOutput)
      rv = NS_OK;
  }

  if (*aOutput && !SchemeIsWhiteListed(*aOutput)) {
    // If URI scheme is not whitelisted and the URI lacks a hostname, force a
    // failure.
    nsAutoCString host;
    rv = (*aOutput)->GetHost(host);
    if (NS_SUCCEEDED(rv) && (host.Length() == 0)) {
      rv = NS_ERROR_FAILURE;
    }
  }

  // Log failure to error console.
  if (aLogErrors && NS_FAILED(rv)) {
    nsCOMPtr<nsIConsoleService> console
                              (do_GetService(NS_CONSOLESERVICE_CONTRACTID));
    if (console) {
      nsCString spec;
      nsCString srcSpec("unknown");

      if (srcURI)
        srcURI->GetSpec(srcSpec);

      if (*aOutput)
        (*aOutput)->GetSpec(spec);
      if (spec.Length() > 0) {
        nsPrintfCString msg("getFirstPartyURI failed for %s: no host in first party URI %s",
                            srcSpec.get(), spec.get()); // TODO: L10N
        console->LogStringMessage(NS_ConvertUTF8toUTF16(msg).get());
      } else {
        nsPrintfCString msg("getFirstPartyURI failed for %s: 0x%x", srcSpec.get(), rv);
        console->LogStringMessage(NS_ConvertUTF8toUTF16(msg).get());
      }
    }

    if (*aOutput) {
      // discard return object.
      (*aOutput)->Release();
      *aOutput = nullptr;
    }
  }

  // TODO: We could provide a route through the loadgroup + notification
  // callbacks too, but either channel or document was always available
  // in the cases where this function was originally needed (the image cache).
  // The notification callbacks also appear to suffers from the same limitation
  // as the document path. See nsICookiePermissions.GetOriginatingURI() for
  // details.

  return rv;
}

NS_IMETHODIMP
ThirdPartyUtil::GetFirstPartyURIFromChannel(nsIChannel *aChannel,
                                            bool aLogErrors,
                                            nsIURI **aOutput)
{
  return GetFirstPartyURIInternal(aChannel, nullptr, aLogErrors, aOutput);
}

NS_IMETHODIMP
ThirdPartyUtil::GetFirstPartyHostForIsolation(nsIURI *aFirstPartyURI,
                                              nsACString& aHost)
{
  if (!aFirstPartyURI)
    return NS_ERROR_INVALID_ARG;

  if (!SchemeIsWhiteListed(aFirstPartyURI)) {
    nsresult rv = GetBaseDomain(aFirstPartyURI, aHost);
    return (aHost.Length() > 0) ? NS_OK : rv;
  }

  // This URI lacks a host, so construct and return a pseudo-host.
  aHost = "--NoFirstPartyHost-";

  // Append the scheme.  To ensure that the pseudo-hosts are consistent
  // when the hacky "moz-safe-about" scheme is used, map it back to "about".
  nsAutoCString scheme;
  nsresult rv = aFirstPartyURI->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);
  if (scheme.Equals("moz-safe-about"))
    aHost.Append("about");
  else
    aHost.Append(scheme);

  // Append the URL's file name (e.g., -browser.xul) or its path (e.g.,
  // -home for about:home)
  nsAutoCString s;
  nsCOMPtr<nsIURL> url = do_QueryInterface(aFirstPartyURI);
  if (url)
    url->GetFileName(s);
  else
    aFirstPartyURI->GetPath(s);

  if (s.Length() > 0) {
    aHost.Append("-");
    aHost.Append(s);
  }

  aHost.Append("--");
  return NS_OK;
}

// static
nsresult
ThirdPartyUtil::GetFirstPartyHost(nsIGlobalObject* aGlobalObject, nsACString& aResult)
{
  nsCString isolationKey;
  nsCOMPtr<nsPIDOMWindow> w = do_QueryInterface(aGlobalObject);
  nsGlobalWindow* window = static_cast<nsGlobalWindow*>(w.get());
  if (window) {
    nsIDocument* doc = window->GetExtantDoc();
    if (doc) {
      return ThirdPartyUtil::GetFirstPartyHost(doc, aResult);
    }
  }
  return NS_OK;
}
