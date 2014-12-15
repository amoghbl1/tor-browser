// # URL-bar domain (first party) isolation test

// This test ensures that if first-party isolation is enabled
// ("privacy.thirdparty.isolate" pref is set to 2) then when a loaded file is cached,
// it is indexed by the URL-bar domain.

// In this test, a number of files are loaded (via IFRAME, LINK, SCRIPT, IMG, OBJECT,
// EMBED, AUDIO, VIDEO, TRACK and XMLHttpRequest) by parent pages with different URL bar
// domains. When isolation is active, we test to confirm that a separate copy of each file
// is cached for each different parent domain. We also test to make sure that when
// isolation is inactive, a single copy of the child page is cached and reused for all
// parent domains.

// In this file, functions are defined in call stack order (later functions call earlier
// functions). Comments are formatted for docco.

/* jshint esnext:true */

// __Mozilla utilities__.
const Cu = Components.utils;
const Ci = Components.interfaces;
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");

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

// __copyObject(obj)__.
// Returns a shallow copy of an object. Works with JS native objects.
let copyObject = function (obj) {
  let result = {};
  for (let k of Object.keys(obj)) {
    result[k] = obj[k];
  }
  return result;
};

// __cacheData()__.
// Returns the current state of the cache.
let cacheData = function () {
  let devices = [],
      cacheEntries = [],
      cacheVisitor = { visitDevice : function (deviceID, deviceInfo) {
                                       devices.push(copyObject(deviceInfo));
                                       return true;
                                     },
                       visitEntry : function (deviceID, entryInfo) {
                                      cacheEntries.push(copyObject(entryInfo));
                                      return true;
                                    },
                       QueryInterface : function(iid) {
                             if (iid.equals(Ci.nsICacheVisitor)) return this;
                       } };
  Services.cache.visitEntries(cacheVisitor);
  return { devices : devices, cacheEntries : cacheEntries };
};

// __loadURLinNewTab(URL)__.
// Opens a new tab at a given URL, and waits for it to load. Times out after 5 sec.
// Returns a promise that resolves to the tab. (Task.jsm coroutine.)
let loadURLinNewTab = function* (URL) {
  let tab = gBrowser.addTab(URL),
      browser = gBrowser.getBrowserForTab(tab),
      result = yield listen(browser, "load", 5000, true);
  return tab;
};

// __countMatchingCacheEntries(cacheEntries, domain, fileSuffix)__.
// Reports how many cache entries contain a given domain name and file suffix.
let countMatchingCacheEntries = function (cacheEntries, domain, fileSuffix) {
  return cacheEntries.map(entry => entry.key)
           .filter(key => key.contains(domain))
           .filter(key => key.contains("thirdPartyChild." + fileSuffix))
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
    // iframe, link, script, img, object, embed, xhr, audio, video, track
    suffixes = ["iframe.html", "link.css", "script.js", "img.png", "object.png",
                "embed.png", "xhr.html", "audio.ogg", "video.ogv", "track.vtt" ];

// __checkCachePopulation(pref, numberOfDomains)__.
// Check if the number of entries found in the cache for each
// embedded file type matches the number we expect, given the 
// number of domains and the isolation state.
let checkCachePopulation = function (pref, numberOfDomains) {
  let expectedEntryCount = (pref === 2) ? (2 * numberOfDomains) : 1;
  // Collect cache data.
  let data = cacheData();
  for (let suffix of suffixes) {
    let foundEntryCount = countMatchingCacheEntries(data.cacheEntries, "example.net", suffix),
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

// __test()__.
// The main testing function.
let test = function () {
  waitForExplicitFinish();
  // Launch a Task.jsm coroutine so we can open tabs and wait for each of them to open,
  // one by one.
  Task.spawn(function* () {
    // Keep original pref value for restoring after the tests.
    let originalPrefValue = Services.prefs.getIntPref(privacyPref);
    // Test the pref with both values: 2 (isolating by first party) or 0 (not isolating)
    for (let pref of [2, 0]) {
      // Clear the cache.
      Services.cache2.clear();
      // Set the pref to desired value
      Services.prefs.setIntPref(privacyPref, pref);
      // Open test tabs
      let tabs = [];
      for (let domain of duplicatedDomains) {
        tabs.push(yield loadURLinNewTab("http://" + domain + ".example.com" + parentPage));
        tabs.push(yield loadURLinNewTab("http://" + domain + ".example.org" + grandParentPage));
      }
      // Run checks to make sure cache has expected number of entries for
      // the chosen pref state.
      checkCachePopulation(pref, domains.length);
      // Clean up by removing tabs.
      tabs.forEach(tab => gBrowser.removeTab(tab));
    }
    // Restore the pref to its original value.
    Services.prefs.setIntPref(privacyPref, originalPrefValue);
    // All tests have now been run.
    finish();
  });
};
