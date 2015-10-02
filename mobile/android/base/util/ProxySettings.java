package org.mozilla.gecko.util;

import java.net.InetSocketAddress;
import java.net.Proxy;

import ch.boye.httpclientandroidlib.HttpHost;

public class ProxySettings {
    private static final String TOR_PROXY_ADDRESS = "127.0.0.1";
    private static final int TOR_PROXY_PORT = 8118;

    public static Proxy getProxy() {
        // TODO make configurable
        return new Proxy(Proxy.Type.HTTP, new InetSocketAddress(TOR_PROXY_ADDRESS, TOR_PROXY_PORT));
    }

    public static HttpHost getProxyHost() {
        // TODO make configurable
        return new HttpHost(TOR_PROXY_ADDRESS, TOR_PROXY_PORT);
    }
}
