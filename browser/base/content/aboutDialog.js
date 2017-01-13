/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Services = object with smart getters for common XPCOM services
Components.utils.import("resource://gre/modules/Services.jsm");
Components.utils.import("resource://gre/modules/AppConstants.jsm");

#ifdef TOR_BROWSER_VERSION
# Add double-quotes back on (stripped by JarMaker.py).
#expand const TOR_BROWSER_VERSION = "__TOR_BROWSER_VERSION__";
#endif

function init(aEvent)
{
  if (aEvent.target != document)
    return;

  try {
    var distroId = Services.prefs.getCharPref("distribution.id");
    if (distroId) {
      var distroVersion = Services.prefs.getCharPref("distribution.version");

      var distroIdField = document.getElementById("distributionId");
      distroIdField.value = distroId + " - " + distroVersion;
      distroIdField.style.display = "block";

      try {
        // This is in its own try catch due to bug 895473 and bug 900925.
        var distroAbout = Services.prefs.getComplexValue("distribution.about",
          Components.interfaces.nsISupportsString);
        var distroField = document.getElementById("distribution");
        distroField.value = distroAbout;
        distroField.style.display = "block";
      }
      catch (ex) {
        // Pref is unset
        Components.utils.reportError(ex);
      }
    }
  }
  catch (e) {
    // Pref is unset
  }

  // Include the build ID and display warning if this is an "a#" (nightly or aurora) build
  let versionField = document.getElementById("version");
  let version = Services.appinfo.version;
  if (/a\d+$/.test(version)) {
    document.getElementById("experimental").hidden = false;
    document.getElementById("communityDesc").hidden = true;
  }

#ifdef TOR_BROWSER_VERSION
  versionField.textContent = TOR_BROWSER_VERSION +
                             " (based on Mozilla Firefox " +
                             versionField.textContent + ")";
#endif

  // Append "(32-bit)" or "(64-bit)" build architecture to the version number:
  let bundle = Services.strings.createBundle("chrome://browser/locale/browser.properties");
  let archResource = Services.appinfo.is64Bit
                     ? "aboutDialog.architecture.sixtyFourBit"
                     : "aboutDialog.architecture.thirtyTwoBit";
  let arch = bundle.GetStringFromName(archResource);
  versionField.textContent += ` (${arch})`;

  if (AppConstants.MOZ_UPDATER) {
    gAppUpdater = new appUpdater();

    let channelLabel = document.getElementById("currentChannel");
    let currentChannelText = document.getElementById("currentChannelText");
    channelLabel.value = UpdateUtils.UpdateChannel;
    if (/^release($|\-)/.test(channelLabel.value))
        currentChannelText.hidden = true;
  }

  if (AppConstants.platform == "macosx") {
    // it may not be sized at this point, and we need its width to calculate its position
    window.sizeToContent();
    window.moveTo((screen.availWidth / 2) - (window.outerWidth / 2), screen.availHeight / 5);
  }
}
