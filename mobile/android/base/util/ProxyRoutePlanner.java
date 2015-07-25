package org.mozilla.gecko.util;

import ch.boye.httpclientandroidlib.HttpException;
import ch.boye.httpclientandroidlib.HttpHost;
import ch.boye.httpclientandroidlib.HttpRequest;
import ch.boye.httpclientandroidlib.impl.conn.DefaultRoutePlanner;
import ch.boye.httpclientandroidlib.impl.conn.DefaultSchemePortResolver;
import ch.boye.httpclientandroidlib.protocol.HttpContext;

public class ProxyRoutePlanner extends DefaultRoutePlanner {
    public ProxyRoutePlanner() {
        super(DefaultSchemePortResolver.INSTANCE);
    }

    @Override
    protected HttpHost determineProxy(
            final HttpHost target,
            final HttpRequest request,
            final HttpContext context) throws HttpException {
        // TODO multiple proxies handling different domains
        return ProxySettings.getProxyHost();
    }
}
