/**
 * Bug 1352305 - A test case for dialog windows that it should not be rounded
 *   even after fingerprinting resistance is enabled.
 */

add_task(async function setup() {
  await SpecialPowers.pushPrefEnv({"set":
    [["privacy.resistFingerprinting", true]]
  });
});

add_task(async function test_dialog_window() {

  let diagWin;

  await new Promise(resolve => {
    // Open a dialog window which is not rounded size.
    diagWin = window.openDialog("about:blank", null,
                                "innerWidth=250,innerHeight=350");

    diagWin.addEventListener("load", function() {
      resolve();
    }, {once: true});
  });

  is(diagWin.innerWidth, 250, "The dialog window doesn't have a rounded size.");
  is(diagWin.innerHeight, 350, "The dialog window doesn't have a rounded size.");

  await BrowserTestUtils.closeWindow(diagWin);
});
