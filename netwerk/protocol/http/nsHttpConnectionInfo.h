/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttpConnectionInfo_h__
#define nsHttpConnectionInfo_h__

#include "nsHttp.h"
#include "nsProxyInfo.h"
#include "nsCOMPtr.h"
#include "nsStringFwd.h"

extern PRLogModuleInfo *gHttpLog;

//-----------------------------------------------------------------------------
// nsHttpConnectionInfo - holds the properties of a connection
//-----------------------------------------------------------------------------

namespace mozilla { namespace net {

class nsHttpConnectionInfo
{
public:
    nsHttpConnectionInfo(const nsACString &host, int32_t port,
                         const nsACString &username,
                         nsProxyInfo* proxyInfo,
                         bool usingSSL=false);

    virtual ~nsHttpConnectionInfo()
    {
        PR_LOG(gHttpLog, 4, ("Destroying nsHttpConnectionInfo @%x\n", this));
    }

    const nsAFlatCString &HashKey() const { return mHashKey; }

    void SetOriginServer(const nsACString &host, int32_t port);

    void SetOriginServer(const char *host, int32_t port)
    {
        SetOriginServer(nsDependentCString(host), port);
    }

    // OK to treat this as an infalible allocation
    nsHttpConnectionInfo* Clone() const;

    const char *ProxyHost() const { return mProxyInfo ? mProxyInfo->Host().get() : nullptr; }
    int32_t     ProxyPort() const { return mProxyInfo ? mProxyInfo->Port() : -1; }
    const char *ProxyType() const { return mProxyInfo ? mProxyInfo->Type() : nullptr; }
    const char *ProxyUsername() const { return mProxyInfo ? mProxyInfo->Username().get() : nullptr; }
    const char *ProxyPassword() const { return mProxyInfo ? mProxyInfo->Password().get() : nullptr; }

    // Compare this connection info to another...
    // Two connections are 'equal' if they end up talking the same
    // protocol to the same server. This is needed to properly manage
    // persistent connections to proxies
    // Note that we don't care about transparent proxies -
    // it doesn't matter if we're talking via socks or not, since
    // a request will end up at the same host.
    bool Equals(const nsHttpConnectionInfo *info)
    {
        return mHashKey.Equals(info->HashKey());
    }

    const char   *Host() const           { return mHost.get(); }
    int32_t       Port() const           { return mPort; }
    const char   *Username() const       { return mUsername.get(); }
    nsProxyInfo  *ProxyInfo()            { return mProxyInfo; }
    bool          UsingHttpProxy() const { return mUsingHttpProxy; }
    bool          UsingSSL() const       { return mUsingSSL; }
    bool          UsingConnect() const   { return mUsingConnect; }
    int32_t       DefaultPort() const    { return mUsingSSL ? NS_HTTPS_DEFAULT_PORT : NS_HTTP_DEFAULT_PORT; }
    void          SetAnonymous(bool anon)
                                         { mHashKey.SetCharAt(anon ? 'A' : '.', 2); }
    bool          GetAnonymous() const   { return mHashKey.CharAt(2) == 'A'; }
    void          SetPrivate(bool priv)  { mHashKey.SetCharAt(priv ? 'P' : '.', 3); }
    bool          GetPrivate() const     { return mHashKey.CharAt(3) == 'P'; }

    const nsCString &GetHost() { return mHost; }

    // Returns true for any kind of proxy (http, socks, etc..)
    bool UsingProxy();

    // Returns true when mHost is an RFC1918 literal.
    bool HostIsLocalIPLiteral() const;

private:
    nsCString              mHashKey;
    nsCString              mHost;
    int32_t                mPort;
    nsCString              mUsername;
    nsCOMPtr<nsProxyInfo>  mProxyInfo;
    bool                   mUsingHttpProxy;
    bool                   mUsingSSL;
    bool                   mUsingConnect;  // if will use CONNECT with http proxy

// for nsRefPtr
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(nsHttpConnectionInfo)
};

}} // namespace mozilla::net

#endif // nsHttpConnectionInfo_h__
