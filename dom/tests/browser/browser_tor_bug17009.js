"use strict";

/*jshint esnext:true */

const CONTENT_PAGE = "http://example.com/browser/dom/tests/browser/browser_tor_bug17009.html";

// __pushPref(key, value)__.
// Set the given pref to a value. Returns a promise
// that resolves asynchronously when the pref is
// successfully set.
let pushPref = function (key, value) {
  return new Promise(resolve => {
    SpecialPowers.pushPrefEnv({"set": [[key, value]]}, resolve);
  });
};

// __listen(target, eventType, useCapture)__.
// Returns a promise that resolves after a single event
// of eventType is received by the target.
let listen = function (target, eventType, useCapture) {
  return new Promise(function (resolve, reject) {
    let listenFunction = function (event) {
      target.removeEventListener(eventType, listenFunction, useCapture);
      resolve(event);
    };
    target.addEventListener(eventType, listenFunction, useCapture);
  });
};

// __soon()__.
// Wait a little bit before we continue. Returns a promise.
let soon = function () {
  return new Promise(resolve => executeSoon(resolve));
};

// __runTests(contentWindow)__.
// The main tests. We test to make sure that, when
// "privacy.suppressModifierKeyEvents" is true, then content
// will not see Alt or Shift keydown or keyup events,
// but chrome will still see those events.
// If the pref is off, then both content and chrome
// should see all events.
let runTests = function* (contentWindow) {
  // Check behavior for both non-resistance and resistance.
  for (let resistFingerprinting of [false, true]) {
    yield pushPref("privacy.resistFingerprinting", resistFingerprinting);
    // Check behavior, both non-suppressing and suppressing.
    for (let suppressModifiers of [false, true]) {
      yield pushPref("privacy.suppressModifierKeyEvents", suppressModifiers);
      // Check both keydown and keyup events.
      for (let eventType of ["keydown", "keyup"]) {
        // Check Alt and Shift keys.
        for (let modifierKey of ["Alt", "Shift"]) {
          // Listen for a single key event in the content window.
          let contentEventPromise = listen(contentWindow, eventType, false);
          // Listen for a single key event in the chrome window.
          let chromeEventPromise = listen(window, eventType, false);
          // Generate a Alt or Shift key event.
          EventUtils.synthesizeKey("VK_" + modifierKey.toUpperCase(),
                                   { type : eventType }, contentWindow);
          // Generate a dummy "x" key event that will only be handled if
          // modifier key is successfully suppressed.
          EventUtils.synthesizeKey("x", { type: eventType }, contentWindow);
          // Collect the events received in content and chrome.
          let contentEvent = yield contentEventPromise;
          let chromeEvent = yield chromeEventPromise;
          // We should always see the modifier key in chrome, regardless of the
          // pref state.
          is(chromeEvent.key, modifierKey,
             modifierKey + " key should be seen in chrome.");
          // If and only if fingerprinting resistance is active and suppression
          // is enabled, we should see the dummy "x" key; otherwise we expect
          // to see the modifier key as usual.
          let expectedContentKey = resistFingerprinting && suppressModifiers
                                   ? "x" : modifierKey;
          is(contentEvent.key, expectedContentKey,
             expectedContentKey + " key should be seen in content.");
        }
      }
    }
  }
};

// Run tests asynchronously to make testing event handling straightforward.
add_task(function* () {
  // Set up a content tab and select it.
  let tab = gBrowser.addTab(CONTENT_PAGE),
      browser = gBrowser.getBrowserForTab(tab);
  gBrowser.selectedTab = tab;

  // Wait for tab to be completely loaded, then run the tests.
  yield listen(browser, "DOMContentLoaded", false);
  yield soon();
  yield runTests(gBrowser.contentWindow);

  // cleanup
  gBrowser.removeTab(tab);
});
