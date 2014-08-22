// # Test Tor Bug #4902: Add DDG and StartPage to Omnibox
// Check what search engines are installed in the search box.

function test() {
  // Grab engine IDs.
  let browserSearchService = Components.classes["@mozilla.org/browser/search-service;1"]
                             .getService(Components.interfaces.nsIBrowserSearchService),
      engineIDs = browserSearchService.getEngines().map(e => e.identifier);

  // Check that we have the correct engines installed, in the right order.
  is(engineIDs[0], "startpage", "Default search engine is startpage");
  is(engineIDs[1], "duckduckgo", "Secondary search engine is duckduckgo");
  is(engineIDs[2], "google", "Google is third search engine");
}
