// __browser_tor_bug18995.js__.
// In this test, we open a private browsing window, and load pages
// that test whether we can call `caches.open("test")`

// Helper function.
// Returns a promise that is fulfilled when the first event of eventype
// arrives from the target.
let listen = function (target, eventType, useCapture) {
  return new Promise(function (resolve, reject) {
    let listenFunction = function (event) {
      target.removeEventListener(eventType, listenFunction, useCapture);
      resolve(event);
    };
    target.addEventListener(eventType, listenFunction, useCapture);
  });
};

// The main test
add_task(function* () {
  // First open the private browsing window
  let privateWin = yield BrowserTestUtils.openNewBrowserWindow({private: true});
  let privateBrowser = privateWin.gBrowser.selectedBrowser;

  // We have two pages: (1) access CacheStorage in content page
  //                    (2) access CacheStorage in worker
  let testURIs = ["http://mochi.test:8888/browser/tbb-tests/bug18995.html",
                  "http://mochi.test:8888/browser/tbb-tests/worker_bug_18995.html"];
  for (let testURI of testURIs) {
    // Load the test page
    privateBrowser.loadURI(testURI);
    // Wait for it too fully load
    yield BrowserTestUtils.browserLoaded(privateBrowser);
    // Get the <div id="result"/> in the content page
    let resultDiv = privateBrowser.contentDocument.getElementById("result");
    // Send an event to the content page indicating we are ready to receive.
    resultDiv.dispatchEvent(new Event("ready"));
    // Wait for a signal from the content page that a result is ready.
    yield listen(resultDiv, "result", false);
    // Read the result from the result <div>
    let resultValue = resultDiv.innerHTML;
    // Print out the result
    info("received: " + resultValue);
    // If we are in PBM, then the promise returned by caches.open(...)
    // is supposed to arrive at a rejection with a SecurityError.
    ok(resultValue.contains("SecurityError"),
       "CacheStorage should fail in private browsing mode");
  }
  // Close the browser window because we are done testing.
  yield BrowserTestUtils.closeWindow(privateWin);
});
