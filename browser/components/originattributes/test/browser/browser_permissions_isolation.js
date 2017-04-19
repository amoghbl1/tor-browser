/**
 * Tor Bug 21569 - A test case for permissions isolation.
 */

const TEST_PAGE = "http://mochi.test:8888/browser/browser/components/" +
                  "originattributes/test/browser/file_firstPartyBasic.html";

function* init() {
  let permPromise = TestUtils.topicObserved("perm-changed");
  Services.perms.removeAll();
  info("called removeAll");
  yield permPromise;
  info("cleared permissions for new test");
}

// Define the testing function
function* doTest(aBrowser) {
  // Promise will result when permissions popup appears:
  let popupShowPromise = BrowserTestUtils.waitForEvent(PopupNotifications.panel, "popupshown");
  let originalStatus = yield ContentTask.spawn(aBrowser, null, function* (key) {
    let status = (yield content.navigator.permissions.query({name: "notifications"})).state;
    content.Notification.requestPermission();
    return status;
  });
  info(`originalStatus: '${originalStatus}'`);
  if (originalStatus === "prompt") {
    // Wait for the popup requesting permission to show notifications:
    yield popupShowPromise;
    let popupHidePromise = BrowserTestUtils.waitForEvent(PopupNotifications.panel, "popuphidden");
    let popupNotification = PopupNotifications.panel.childNodes[0];
    // Click to grant permission:
    popupNotification.button.click();
    // Wait for popup to hide again.
    yield popupHidePromise;
  }
  return originalStatus;
}

add_task(function* () {
    yield SpecialPowers.pushPrefEnv({
      set: [["dom.webnotifications.enabled", true]]
    });
    IsolationTestTools.runTests(TEST_PAGE, doTest,
                                (isolated, val1, val2) => (isolated === ( val2 === "prompt")),
                                init, true);
});
