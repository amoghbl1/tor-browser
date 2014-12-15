// # URL-bar domain (first party) isolation test

// This test ensures that if first-party isolation is enabled
// ("privacy.thirdparty.isolate" pref is set to 2) then when a loaded file is cached,
// it is indexed by the URL-bar domain.

// In this test, a number of files are loaded (via IFRAME, LINK [both CSS
// and Favicon], SCRIPT, IMG, OBJECT, EMBED, AUDIO, VIDEO, TRACK and
// XMLHttpRequest) by parent pages with different URL bar domains. When
// isolation is active, we test to confirm that a separate copy of each file is
// cached for each different parent domain. We also test to make sure that when
// isolation is inactive, a single copy of the child page is cached
// and reused for all parent domains.

// In this file, functions are defined in call stack order (later functions call earlier
// functions). Comments are formatted for docco.

/* jshint esnext:true */

// __Mozilla utilities__.
const Cu = Components.utils;
const Ci = Components.interfaces;
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");
let tempScope = {}; // Avoid 'leaked window property' error.
Cu.import("resource://gre/modules/LoadContextInfo.jsm", tempScope);
let LoadContextInfo = tempScope.LoadContextInfo;
let thirdPartyUtil = Cc["@mozilla.org/thirdpartyutil;1"]
                       .getService(Ci.mozIThirdPartyUtil);
let protocolProxyService = Cc["@mozilla.org/network/protocol-proxy-service;1"]
                             .getService(Ci.nsIProtocolProxyService);

// __listen(target, eventType, timeoutMs, useCapture)__.
// Calls addEventListener on target, with the given eventType.
// Returns a Promise that resolves to an Event object, if the event fires.
// If a timeout occurs, then Promise is rejected with a "Timed out" error.
// For use with Task.jsm.
let listen = function (target, eventType, timeoutMs, useCapture) {
  return new Promise(function (resolve, reject) {
    let listenFunction = function (event) {
      target.removeEventListener(eventType, listenFunction, useCapture);
      resolve(event);
    };
    target.addEventListener(eventType, listenFunction, useCapture);
    window.setTimeout(() => reject(new Error("Timed out")), timeoutMs);
  });
};


// __cacheDataForContext()__.
// Returns a promise that resolves to an array of cache entries for
// loadContextInfo.  Each element of the array is an object with the
// following properties:
//   uri - nsIURI object
//   idEnhance - extra key info (e.g., first party domain)
let cacheDataForContext = function(loadContextInfo, timeoutMs)
{
  return new Promise(function(resolve, reject) {
    let cacheEntries = [];
    let cacheVisitor = {  onCacheStorageInfo: function(num, consumption) {},
                          onCacheEntryInfo: function(uri, idEnhance) {
                            cacheEntries.push({ uri: uri,
                                                idEnhance: idEnhance });
                          },
                          onCacheEntryVisitCompleted: function() {
                            resolve(cacheEntries);
                          },
                          QueryInterface : function(iid) {
                            if (iid.equals(Ci.nsICacheStorageVisitor))
                              return this;
                          }
                      };
    // Visiting the disk cache also visits memory storage so we do not
    // need to use Services.cache2.memoryCacheStorage() here.
    let storage = Services.cache2.diskCacheStorage(loadContextInfo, false);
    storage.asyncVisitStorage(cacheVisitor, true);

    window.setTimeout(() => reject(new Error("Timed out")), timeoutMs);
  });
}


// __loadURLinNewTab(URL)__.
// Opens a new tab at a given URL, and waits for it to load. Times out after 5 sec.
// Returns a promise that resolves to the tab. (Task.jsm coroutine.)
let loadURLinNewTab = function* (URL) {
  let tab = gBrowser.addTab(URL),
      browser = gBrowser.getBrowserForTab(tab),
      result = yield listen(browser, "load", 10000, true);
  return tab;
};

// __countMatchingCacheEntries(cacheEntries, domain, fileSuffix)__.
// Reports how many cache entries contain a given domain name and file suffix.
let countMatchingCacheEntries = function (cacheEntries, domain, fileSuffix) {
  return cacheEntries.map(entry => entry.uri.asciiSpec)
           .filter(spec => spec.contains(domain))
           .filter(spec => spec.contains("thirdPartyChild." + fileSuffix))
           .length;
};

// __Constants__.
let privacyPref = "privacy.thirdparty.isolate",
    parentPage = "/browser/netwerk/test/browser/firstPartyParent.html",
    grandParentPage = "/browser/netwerk/test/browser/firstPartyGrandParent.html",
    // Parent domains (the iframe "child" domain is example.net):
    domains = ["test1", "test2"],
    // We duplicate domains, to check that two pages with the same first party domain
    // share cached embedded objects.
    duplicatedDomains = [].concat(domains, domains),
    // We will check cache for example.net content from
    // iframe, link (css), script, img, object, embed, xhr, audio, video,
    // track, favicon, fetch, request
    suffixes = ["iframe.html", "link.css", "script.js", "img.png", "object.png",
                "embed.png", "xhr.html", "worker.xhr.html", "audio.ogg",
                "video.ogv", "track.vtt", "favicon.ico",
                "fetch.html", "worker.fetch.html",
                "request.html", "worker.request.html",
                "import.js"];

// __checkCachePopulation(isolate, numberOfDomains)__.
// Check if the number of entries found in the cache for each
// embedded file type matches the number we expect, given the
// number of domains and the isolation state.
let checkCachePopulation = function* (isolate, numberOfDomains) {
  let expectedEntryCount = isolate ? numberOfDomains : 1;
  // Collect cache data.
  let data = yield cacheDataForContext(LoadContextInfo.default, 2000);
  data = data.concat(yield cacheDataForContext(LoadContextInfo.private, 2000));
  // `fetch` is cached in storage where both private and anonymous flags are set to true.
  data = data.concat(yield cacheDataForContext(LoadContextInfo.custom(true, true, {appid : null}), 2000));
/*
  // Uncomment to log all of the cache entries (useful for debugging).
  for (let cacheEntry of data) {
    info("cache uri: " + cacheEntry.uri.asciiSpec
         + ", idEnhance: " + cacheEntry.idEnhance);
  }
*/
  for (let suffix of suffixes) {
    let foundEntryCount = countMatchingCacheEntries(data, "example.net", suffix),
        result = (suffix.startsWith("video") || suffix.startsWith("audio")) ?
          // Video and audio elements aren't always cached, so
          // tolerate fewer cached copies.
          (expectedEntryCount === 1 ? (foundEntryCount <= 1) : (foundEntryCount > 1)) :
          (expectedEntryCount === foundEntryCount);
    // Report results to mochitest
    ok(result, "Cache entries expected for " + suffix +
                           ": " + expectedEntryCount);
  }
};

// __observeChannels(onChannel)__.
// onChannel is called for every channel request. Returns a zero-arg stop() function.
let observeChannels = function (onChannel) {
  // We use a dummy proxy filter to catch all channels, even those that do not
  // generate an "http-on-modify-request" notification, such as link preconnects.
  let proxyFilter = {
    applyFilter : function (aProxyService, aChannel, aProxy) {
      // We have the channel; provide it to the callback.
      onChannel(aChannel);
      // Pass on aProxy unmodified.
      return aProxy;
    }
  };
  protocolProxyService.registerChannelFilter(proxyFilter, 0);
  // Return the stop() function:
  return () => protocolProxyService.unregisterChannelFilter(proxyFilter);
};

// __channelFirstPartyHost(aChannel)__.
// Returns the first-party host for the given channel.
let channelFirstPartyHost = function (aChannel) {
  let channel = aChannel.QueryInterface(Ci.nsIChannel),
      firstPartyURI = thirdPartyUtil.getFirstPartyURIFromChannel(channel, true)
                                    .QueryInterface(Ci.nsIURI);
  return thirdPartyUtil.getFirstPartyHostForIsolation(firstPartyURI);
}

// __startObservingChannels()__.
// Checks to see if each channel has the correct first party assigned.
// All "thirdPartyChild" resources are loaded from a third-party
// "example.net" host, but they should all report either an "example.com"
// or an "example.org" first-party domain. Returns a stop() function.
let startObservingChannels = function() {
  let stopObservingChannels = observeChannels(function (channel) {
    let originalURISpec = channel.originalURI.spec;
    if (originalURISpec.contains("example.net")) {
      let firstPartyHost = channelFirstPartyHost(channel);
      ok(firstPartyHost === "example.com" || firstPartyHost === "example.org", "first party for " + originalURISpec + " is " + firstPartyHost);
    }
  });
  return stopObservingChannels;
};

// The main testing function.
// Launch a Task.jsm coroutine so we can open tabs and wait for each of them to open,
// one by one.
add_task(function* () {
  // Keep original pref value for restoring after the tests.
  let originalPrefValue = Services.prefs.getIntPref(privacyPref);
  // Test the pref with both values: 2 (isolating by first party) or 0 (not isolating)
  for (let isolate of [true, false]) {
    let stopObservingChannels;
    if (isolate) {
      stopObservingChannels = startObservingChannels();
    }
    // Clear the cache.
    Services.cache2.clear();
    // Set the pref to desired value
    Services.prefs.setIntPref(privacyPref, isolate ? 2 : 0);
    // Open test tabs
    let tabs = [];
    for (let domain of duplicatedDomains) {
      tabs.push(yield loadURLinNewTab("http://" + domain + ".example.com" + parentPage));
      tabs.push(yield loadURLinNewTab("http://" + domain + ".example.org" + grandParentPage));
    }
    // Run checks to make sure cache has expected number of entries for
    // the chosen isolation state.
    let firstPartyDomainCount = 2; // example.com and example.org
    yield checkCachePopulation(isolate, firstPartyDomainCount);
    // Clean up by removing tabs.
    tabs.forEach(tab => gBrowser.removeTab(tab));
    if (isolate) {
      stopObservingChannels();
    }
  }
  // Restore the pref to its original value.
  Services.prefs.setIntPref(privacyPref, originalPrefValue);
});
