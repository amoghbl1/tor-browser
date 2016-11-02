/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHostObjectProtocolHandler.h"
#include "nsHostObjectURI.h"
#include "nsError.h"
#include "nsClassHashtable.h"
#include "nsNetUtil.h"
#include "nsIPrincipal.h"
#include "DOMMediaStream.h"
#include "mozilla/dom/MediaSource.h"
#include "nsIMemoryReporter.h"
#include "mozilla/dom/File.h"
#include "mozilla/Preferences.h"
#include "mozilla/LoadInfo.h"
#include "ThirdPartyUtil.h"

using mozilla::dom::FileImpl;
using mozilla::ErrorResult;
using mozilla::LoadInfo;

// -----------------------------------------------------------------------
// Hash table
struct DataInfo
{
  // mObject is expected to be an nsIDOMBlob, DOMMediaStream, or MediaSource
  nsCOMPtr<nsISupports> mObject;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCString mStack;
  nsCString mFirstPartyHost;
};

static nsClassHashtable<nsCStringHashKey, DataInfo>* gDataTable;
static nsCOMPtr<mozIThirdPartyUtil> gThirdPartyUtilService;

/**
//defined but not used
static nsCString GetFirstPartyHostFromCaller() {
  if (!gThirdPartyUtilService) {
    gThirdPartyUtilService = do_GetService(THIRDPARTYUTIL_CONTRACTID);
  }
  nsCString host;
  gThirdPartyUtilService->GetFirstPartyHostFromCaller(host);
  return host;
}
*/

// Memory reporting for the hash table.
namespace mozilla {

class HostObjectURLsReporter final : public nsIMemoryReporter
{
  ~HostObjectURLsReporter() {}

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override
  {
    return MOZ_COLLECT_REPORT(
      "host-object-urls", KIND_OTHER, UNITS_COUNT,
      gDataTable ? gDataTable->Count() : 0,
      "The number of host objects stored for access via URLs "
      "(e.g. blobs passed to URL.createObjectURL).");
  }
};

NS_IMPL_ISUPPORTS(HostObjectURLsReporter, nsIMemoryReporter)

class BlobURLsReporter final : public nsIMemoryReporter
{
 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aCallback,
                            nsISupports* aData, bool aAnonymize) override
  {
    EnumArg env;
    env.mCallback = aCallback;
    env.mData = aData;
    env.mAnonymize = aAnonymize;

    if (gDataTable) {
      gDataTable->EnumerateRead(CountCallback, &env);
      gDataTable->EnumerateRead(ReportCallback, &env);
    }
    return NS_OK;
  }

  // Initialize info->mStack to record JS stack info, if enabled.
  // The string generated here is used in ReportCallback, below.
  static void GetJSStackForBlob(DataInfo* aInfo)
  {
    nsCString& stack = aInfo->mStack;
    MOZ_ASSERT(stack.IsEmpty());
    const uint32_t maxFrames = Preferences::GetUint("memory.blob_report.stack_frames");

    if (maxFrames == 0) {
      return;
    }

    nsresult rv;
    nsIXPConnect* xpc = nsContentUtils::XPConnect();
    nsCOMPtr<nsIStackFrame> frame;
    rv = xpc->GetCurrentJSStack(getter_AddRefs(frame));
    NS_ENSURE_SUCCESS_VOID(rv);

    nsAutoCString origin;
    nsCOMPtr<nsIURI> principalURI;
    if (NS_SUCCEEDED(aInfo->mPrincipal->GetURI(getter_AddRefs(principalURI)))
        && principalURI) {
      principalURI->GetPrePath(origin);
    }

    for (uint32_t i = 0; i < maxFrames && frame; ++i) {
      nsString fileNameUTF16;
      int32_t lineNumber = 0;

      frame->GetFilename(fileNameUTF16);
      frame->GetLineNumber(&lineNumber);

      if (!fileNameUTF16.IsEmpty()) {
        NS_ConvertUTF16toUTF8 fileName(fileNameUTF16);
        stack += "js(";
        if (!origin.IsEmpty()) {
          // Make the file name root-relative for conciseness if possible.
          const char* originData;
          uint32_t originLen;

          originLen = origin.GetData(&originData);
          // If fileName starts with origin + "/", cut up to that "/".
          if (fileName.Length() >= originLen + 1 &&
              memcmp(fileName.get(), originData, originLen) == 0 &&
              fileName[originLen] == '/') {
            fileName.Cut(0, originLen);
          }
        }
        fileName.ReplaceChar('/', '\\');
        stack += fileName;
        if (lineNumber > 0) {
          stack += ", line=";
          stack.AppendInt(lineNumber);
        }
        stack += ")/";
      }

      rv = frame->GetCaller(getter_AddRefs(frame));
      NS_ENSURE_SUCCESS_VOID(rv);
    }
  }

 private:
  ~BlobURLsReporter() {}

  struct EnumArg {
    nsIHandleReportCallback* mCallback;
    nsISupports* mData;
    bool mAnonymize;
    nsDataHashtable<nsPtrHashKey<nsIDOMBlob>, uint32_t> mRefCounts;
  };

  // Determine number of URLs per blob, to handle the case where it's > 1.
  static PLDHashOperator CountCallback(nsCStringHashKey::KeyType aKey,
                                       DataInfo* aInfo,
                                       void* aUserArg)
  {
    EnumArg* envp = static_cast<EnumArg*>(aUserArg);
    nsCOMPtr<nsIDOMBlob> blob;

    blob = do_QueryInterface(aInfo->mObject);
    if (blob) {
      envp->mRefCounts.Put(blob, envp->mRefCounts.Get(blob) + 1);
    }
    return PL_DHASH_NEXT;
  }

  static PLDHashOperator ReportCallback(nsCStringHashKey::KeyType aKey,
                                        DataInfo* aInfo,
                                        void* aUserArg)
  {
    EnumArg* envp = static_cast<EnumArg*>(aUserArg);
    nsCOMPtr<nsIDOMBlob> blob;

    blob = do_QueryInterface(aInfo->mObject);
    if (blob) {
      NS_NAMED_LITERAL_CSTRING
        (desc, "A blob URL allocated with URL.createObjectURL; the referenced "
         "blob cannot be freed until all URLs for it have been explicitly "
         "invalidated with URL.revokeObjectURL.");
      nsAutoCString path, url, owner, specialDesc;
      nsCOMPtr<nsIURI> principalURI;
      uint64_t size = 0;
      uint32_t refCount = 1;
      DebugOnly<bool> blobWasCounted;

      blobWasCounted = envp->mRefCounts.Get(blob, &refCount);
      MOZ_ASSERT(blobWasCounted);
      MOZ_ASSERT(refCount > 0);

      bool isMemoryFile = blob->IsMemoryFile();

      if (isMemoryFile) {
        if (NS_FAILED(blob->GetSize(&size))) {
          size = 0;
        }
      }

      path = isMemoryFile ? "memory-blob-urls/" : "file-blob-urls/";
      if (NS_SUCCEEDED(aInfo->mPrincipal->GetURI(getter_AddRefs(principalURI))) &&
          principalURI != nullptr &&
          NS_SUCCEEDED(principalURI->GetSpec(owner)) &&
          !owner.IsEmpty()) {
        owner.ReplaceChar('/', '\\');
        path += "owner(";
        if (envp->mAnonymize) {
          path += "<anonymized>";
        } else {
          path += owner;
        }
        path += ")";
      } else {
        path += "owner unknown";
      }
      path += "/";
      if (envp->mAnonymize) {
        path += "<anonymized-stack>";
      } else {
        path += aInfo->mStack;
      }
      url = aKey;
      url.ReplaceChar('/', '\\');
      if (envp->mAnonymize) {
        path += "<anonymized-url>";
      } else {
        path += url;
      }
      if (refCount > 1) {
        nsAutoCString addrStr;

        addrStr = "0x";
        addrStr.AppendInt((uint64_t)(nsIDOMBlob*)blob, 16);

        path += " ";
        path.AppendInt(refCount);
        path += "@";
        path += addrStr;

        specialDesc = desc;
        specialDesc += "\n\nNOTE: This blob (address ";
        specialDesc += addrStr;
        specialDesc += ") has ";
        specialDesc.AppendInt(refCount);
        specialDesc += " URLs.";
        if (isMemoryFile) {
          specialDesc += " Its size is divided ";
          specialDesc += refCount > 2 ? "among" : "between";
          specialDesc += " them in this report.";
        }
      }

      const nsACString& descString = specialDesc.IsEmpty()
          ? static_cast<const nsACString&>(desc)
          : static_cast<const nsACString&>(specialDesc);
      if (isMemoryFile) {
        envp->mCallback->Callback(EmptyCString(),
            path,
            KIND_OTHER,
            UNITS_BYTES,
            size / refCount,
            descString,
            envp->mData);
      }
      else {
        envp->mCallback->Callback(EmptyCString(),
            path,
            KIND_OTHER,
            UNITS_COUNT,
            1,
            descString,
            envp->mData);
      }
    }
    return PL_DHASH_NEXT;
  }
};

NS_IMPL_ISUPPORTS(BlobURLsReporter, nsIMemoryReporter)

}

void
nsHostObjectProtocolHandler::Init(void)
{
  static bool initialized = false;

  if (!initialized) {
    initialized = true;
    RegisterStrongMemoryReporter(new mozilla::HostObjectURLsReporter());
    RegisterStrongMemoryReporter(new mozilla::BlobURLsReporter());
  }
}

nsHostObjectProtocolHandler::nsHostObjectProtocolHandler()
{
  Init();
}

nsresult
nsHostObjectProtocolHandler::AddDataEntry(const nsACString& aScheme,
                                          nsISupports* aObject,
                                          nsIPrincipal* aPrincipal,
                                          const nsACString& aIsolationKey,
                                          nsACString& aUri)
{
  Init();

  nsresult rv = GenerateURIString(aScheme, aPrincipal, aUri);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!gDataTable) {
    gDataTable = new nsClassHashtable<nsCStringHashKey, DataInfo>;
  }

  DataInfo* info = new DataInfo;

  info->mObject = aObject;
  info->mPrincipal = aPrincipal;
  // Record the first party host that originated this object.
  info->mFirstPartyHost = aIsolationKey;
  mozilla::BlobURLsReporter::GetJSStackForBlob(info);

  gDataTable->Put(aUri, info);
  return NS_OK;
}

static DataInfo*
GetDataInfo(const nsACString& aUri)
{
  if (!gDataTable) {
    return nullptr;
  }

  DataInfo* res;
  nsCString uriIgnoringRef;
  int32_t hashPos = aUri.FindChar('#');
  if (hashPos < 0) {
    uriIgnoringRef = aUri;
  }
  else {
    uriIgnoringRef = StringHead(aUri, hashPos);
  }
  gDataTable->Get(uriIgnoringRef, &res);

  return res;
}

void
nsHostObjectProtocolHandler::RemoveDataEntry(const nsACString& aUri,
                                             const nsACString& aIsolationKey)
{
  if (gDataTable) {
    DataInfo* info = GetDataInfo(aUri);
    if (info && info->mFirstPartyHost == aIsolationKey) {
      nsCString uriIgnoringRef;
      int32_t hashPos = aUri.FindChar('#');
      if (hashPos < 0) {
        uriIgnoringRef = aUri;
      }
      else {
        uriIgnoringRef = StringHead(aUri, hashPos);
      }
      gDataTable->Remove(uriIgnoringRef);
      if (gDataTable->Count() == 0) {
        delete gDataTable;
        gDataTable = nullptr;
      }
    }
  }
}

nsresult
nsHostObjectProtocolHandler::GenerateURIString(const nsACString &aScheme,
                                               nsIPrincipal* aPrincipal,
                                               nsACString& aUri)
{
  nsresult rv;
  nsCOMPtr<nsIUUIDGenerator> uuidgen =
    do_GetService("@mozilla.org/uuid-generator;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsID id;
  rv = uuidgen->GenerateUUIDInPlace(&id);
  NS_ENSURE_SUCCESS(rv, rv);

  char chars[NSID_LENGTH];
  id.ToProvidedString(chars);

  aUri = aScheme;
  aUri.Append(':');

  if (aPrincipal) {
    nsAutoString origin;
    rv = nsContentUtils::GetUTFOrigin(aPrincipal, origin);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    AppendUTF16toUTF8(origin, aUri);
    aUri.Append('/');
  }

  aUri += Substring(chars + 1, chars + NSID_LENGTH - 2);

  return NS_OK;
}

nsIPrincipal*
nsHostObjectProtocolHandler::GetDataEntryPrincipal(const nsACString& aUri)
{
  if (!gDataTable) {
    return nullptr;
  }

  DataInfo* res = GetDataInfo(aUri);

  if (!res) {
    return nullptr;
  }

  return res->mPrincipal;
}

void
nsHostObjectProtocolHandler::Traverse(const nsACString& aUri,
                                      nsCycleCollectionTraversalCallback& aCallback)
{
  if (!gDataTable) {
    return;
  }

  DataInfo* res;
  gDataTable->Get(aUri, &res);
  if (!res) {
    return;
  }

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCallback, "HostObjectProtocolHandler DataInfo.mObject");
  aCallback.NoteXPCOMChild(res->mObject);
}

static nsISupports*
GetDataObject(nsIURI* aURI, const nsACString& aIsolationKey)
{
  nsCString spec;
  aURI->GetSpec(spec);

  DataInfo* info = GetDataInfo(spec);
  // Deny access to this object if the current first-party host
  // doesn't match the originating first-party host.
  return (info && info->mFirstPartyHost == aIsolationKey)
         ? info->mObject : nullptr;
}

// -----------------------------------------------------------------------
// Protocol handler

NS_IMPL_ISUPPORTS(nsHostObjectProtocolHandler, nsIProtocolHandler)

NS_IMETHODIMP
nsHostObjectProtocolHandler::GetDefaultPort(int32_t *result)
{
  *result = -1;
  return NS_OK;
}

NS_IMETHODIMP
nsHostObjectProtocolHandler::GetProtocolFlags(uint32_t *result)
{
  *result = URI_NORELATIVE | URI_NOAUTH | URI_LOADABLE_BY_SUBSUMERS |
            URI_IS_LOCAL_RESOURCE | URI_NON_PERSISTABLE;
  return NS_OK;
}

NS_IMETHODIMP
nsHostObjectProtocolHandler::NewURI(const nsACString& aSpec,
                                    const char *aCharset,
                                    nsIURI *aBaseURI,
                                    nsIURI **aResult)
{
  *aResult = nullptr;
  nsresult rv;

  DataInfo* info = GetDataInfo(aSpec);

  nsRefPtr<nsHostObjectURI> uri =
    new nsHostObjectURI(info ? info->mPrincipal.get() : nullptr);

  rv = uri->SetSpec(aSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_TryToSetImmutable(uri);
  uri.forget(aResult);

  return NS_OK;
}

NS_IMETHODIMP
nsHostObjectProtocolHandler::NewChannel2(nsIURI* uri,
                                         nsILoadInfo* aLoadInfo,
                                         nsIChannel** result)
{
  *result = nullptr;

  nsCString spec;
  uri->GetSpec(spec);

  nsCString firstPartyHost;
  if (aLoadInfo) {
    nsCOMPtr<nsIDOMDocument> loadingDOMDocument;
    aLoadInfo->GetLoadingDocument(getter_AddRefs(loadingDOMDocument));
    nsCOMPtr<nsIDocument> loadingDocument = do_QueryInterface(loadingDOMDocument);
    nsresult rv = ThirdPartyUtil::GetFirstPartyHost(loadingDocument, firstPartyHost);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  DataInfo* info = GetDataInfo(spec);
  // Deny access to this URI if the current first party host
  // doesn't match the first party host when it was created.
  if (!info || (info->mFirstPartyHost != firstPartyHost)) {
    return NS_ERROR_DOM_BAD_URI;
  }

  nsCOMPtr<FileImpl> blob = do_QueryInterface(info->mObject);
  if (!blob) {
    return NS_ERROR_DOM_BAD_URI;
  }

#ifdef DEBUG
  {
    nsCOMPtr<nsIURIWithPrincipal> uriPrinc = do_QueryInterface(uri);
    nsCOMPtr<nsIPrincipal> principal;
    uriPrinc->GetPrincipal(getter_AddRefs(principal));
    NS_ASSERTION(info->mPrincipal == principal, "Wrong principal!");
  }
#endif

  nsCOMPtr<nsIInputStream> stream;
  nsresult rv = blob->GetInternalStream(getter_AddRefs(stream));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewInputStreamChannelInternal(getter_AddRefs(channel),
                                        uri,
                                        stream,
                                        EmptyCString(), // aContentType
                                        EmptyCString(), // aContentCharset
                                        aLoadInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString type;
  blob->GetType(type);

  if (blob->IsFile()) {
    nsString filename;
    blob->GetName(filename);
    channel->SetContentDispositionFilename(filename);
  }

  ErrorResult error;
  uint64_t size = blob->GetSize(error);
  if (NS_WARN_IF(error.Failed())) {
    return error.ErrorCode();
  }

  channel->SetOriginalURI(uri);
  channel->SetContentType(NS_ConvertUTF16toUTF8(type));
  channel->SetContentLength(size);

  channel.forget(result);

  return NS_OK;
}

NS_IMETHODIMP
nsHostObjectProtocolHandler::NewChannel(nsIURI* uri, nsIChannel* *result)
{
  return NewChannel2(uri, nullptr, result);
}

NS_IMETHODIMP
nsHostObjectProtocolHandler::AllowPort(int32_t port, const char *scheme,
                                       bool *_retval)
{
  // don't override anything.
  *_retval = false;
  return NS_OK;
}

NS_IMETHODIMP
nsBlobProtocolHandler::GetScheme(nsACString &result)
{
  result.AssignLiteral(BLOBURI_SCHEME);
  return NS_OK;
}

NS_IMETHODIMP
nsMediaStreamProtocolHandler::GetScheme(nsACString &result)
{
  result.AssignLiteral(MEDIASTREAMURI_SCHEME);
  return NS_OK;
}

NS_IMETHODIMP
nsMediaSourceProtocolHandler::GetScheme(nsACString &result)
{
  result.AssignLiteral(MEDIASOURCEURI_SCHEME);
  return NS_OK;
}

NS_IMETHODIMP
nsFontTableProtocolHandler::GetScheme(nsACString &result)
{
  result.AssignLiteral(FONTTABLEURI_SCHEME);
  return NS_OK;
}

nsresult
NS_GetBlobForBlobURI(nsIURI* aURI, const nsACString& aIsolationKey, FileImpl** aBlob)
{
  NS_ASSERTION(IsBlobURI(aURI), "Only call this with blob URIs");

  *aBlob = nullptr;

  nsCOMPtr<FileImpl> blob = do_QueryInterface(GetDataObject(aURI, aIsolationKey));
  if (!blob) {
    return NS_ERROR_DOM_BAD_URI;
  }

  blob.forget(aBlob);
  return NS_OK;
}

nsresult
NS_GetStreamForBlobURI(nsIURI* aURI,
                       const nsACString& aIsolationKey,
                       nsIInputStream** aStream)
{
  nsRefPtr<FileImpl> blobImpl;
  nsresult rv = NS_GetBlobForBlobURI(aURI, aIsolationKey, getter_AddRefs(blobImpl));
  if (NS_FAILED(rv)) {
    return rv;
  }

  return blobImpl->GetInternalStream(aStream);
}

nsresult
NS_GetStreamForMediaStreamURI(nsIURI* aURI,
                              const nsACString& aIsolationKey,
                              mozilla::DOMMediaStream** aStream)
{
  NS_ASSERTION(IsMediaStreamURI(aURI), "Only call this with mediastream URIs");

  nsISupports* dataObject = GetDataObject(aURI, aIsolationKey);
  if (!dataObject) {
    return NS_ERROR_DOM_BAD_URI;
  }

  *aStream = nullptr;
  return CallQueryInterface(dataObject, aStream);
}

NS_IMETHODIMP
nsFontTableProtocolHandler::NewURI(const nsACString& aSpec,
                                   const char *aCharset,
                                   nsIURI *aBaseURI,
                                   nsIURI **aResult)
{
  nsRefPtr<nsIURI> uri;

  // Either you got here via a ref or a fonttable: uri
  if (aSpec.Length() && aSpec.CharAt(0) == '#') {
    nsresult rv = aBaseURI->CloneIgnoringRef(getter_AddRefs(uri));
    NS_ENSURE_SUCCESS(rv, rv);

    uri->SetRef(aSpec);
  } else {
    // Relative URIs (other than #ref) are not meaningful within the
    // fonttable: scheme.
    // If aSpec is a relative URI -other- than a bare #ref,
    // this will leave uri empty, and we'll return a failure code below.
    uri = new nsSimpleURI();
    uri->SetSpec(aSpec);
  }

  bool schemeIs;
  if (NS_FAILED(uri->SchemeIs(FONTTABLEURI_SCHEME, &schemeIs)) || !schemeIs) {
    NS_WARNING("Non-fonttable spec in nsFontTableProtocolHander");
    return NS_ERROR_NOT_AVAILABLE;
  }

  uri.forget(aResult);
  return NS_OK;
}

nsresult
NS_GetSourceForMediaSourceURI(nsIURI* aURI,
                              const nsACString& aIsolationKey,
                              mozilla::dom::MediaSource** aSource)
{
  NS_ASSERTION(IsMediaSourceURI(aURI), "Only call this with mediasource URIs");

  *aSource = nullptr;

  nsCOMPtr<mozilla::dom::MediaSource> source = do_QueryInterface(GetDataObject(aURI, aIsolationKey));
  if (!source) {
    return NS_ERROR_DOM_BAD_URI;
  }

  source.forget(aSource);
  return NS_OK;
}
