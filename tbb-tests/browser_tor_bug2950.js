// # Regression tests for tor Bug #2950, Make Permissions Manager memory-only
// Ensures that permissions.sqlite file in profile directory is not written to,
// even when we write a value to Firefox's permissions database.

// The requisite test() function.
function test() {

// Needed because of asynchronous part later in the test.
waitForExplicitFinish();

// Shortcut
let Ci = Components.interfaces;

// ## utility functions

// __uri(spec)__.
// Creates an nsIURI instance from a spec
// (string address such as "http://torproject.org").
let uri = spec => Services.io.newURI(spec, null, null);

// __setPermission(spec, key, value)__.
// Sets the site permission of type key to value, for the site located at address spec.
let setPermission = (spec, key, value) => SitePermissions.set(uri(spec), key, value);

// __getPermission(spec, key)__.
// Reads the site permission value for permission type key, for the site
// located at address spec.
let getPermission = (spec, key) => SitePermissions.get(uri(spec), key);

// __profileDirPath__.
// The Firefox Profile directory. Expected location of various persistent files.
let profileDirPath = Services.dirsvc.get("ProfD", Components.interfaces.nsIFile).path;

// __fileInProfile(fileName)__.
// Returns an nsIFile instance corresponding to a file in the Profile directory.
let fileInProfile = fileName => FileUtils.File(profileDirPath + "/" + fileName);

// ## Now let's run the test.

let SITE = "http://torproject.org",
    KEY = "popup";

let permissionsFile = fileInProfile("permissions.sqlite"),
                      lastModifiedTime = null,
                      newModifiedTime = null;
if (permissionsFile.exists()) {
  lastModifiedTime = permissionsFile.lastModifiedTime;
}
// Read the original value of the permission.
let originalValue = getPermission(SITE, KEY);

// We need to delay by at least 1000 ms, because that's the granularity
// of file time stamps, it seems.
window.setTimeout(
  function () {
    // Set the permission to a new value.
    setPermission(SITE, KEY, (originalValue === 0) ? 1 : 0);
    // Now read back the permission value again.
    let newReadValue = getPermission(SITE, KEY);
    // Compare to confirm that the permission
    // value was successfully changed.
    isnot(newReadValue, originalValue, "Set a value in permissions db (perhaps in memory).");;
    // If file existed or now exists, get the current time stamp.
    if (permissionsFile.exists()) {
      newModifiedTime = permissionsFile.lastModifiedTime;
    }
    // If file was created or modified since we began this test,
    // then permissions db is not memory only. Complain!
    is(lastModifiedTime, newModifiedTime, "Don't write to permissions.sqlite file on disk.");
    // We are done with the test.
    finish();
  }, 1100);

} // test()
