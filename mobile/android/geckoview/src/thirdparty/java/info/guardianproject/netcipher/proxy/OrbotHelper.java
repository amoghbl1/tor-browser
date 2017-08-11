
package info.guardianproject.netcipher.proxy;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.text.TextUtils;
import android.util.Log;


import java.util.List;

public class OrbotHelper {

    private final static int REQUEST_CODE_STATUS = 100;

    public final static String ORBOT_PACKAGE_NAME = "org.torproject.android";
    public final static String ORBOT_MARKET_URI = "market://details?id=" + ORBOT_PACKAGE_NAME;
    public final static String ORBOT_FDROID_URI = "https://f-droid.org/repository/browse/?fdid="
            + ORBOT_PACKAGE_NAME;
    public final static String ORBOT_PLAY_URI = "https://play.google.com/store/apps/details?id="
            + ORBOT_PACKAGE_NAME;

    /**
     * A request to Orbot to transparently start Tor services
     */
    public final static String ACTION_START = "org.torproject.android.intent.action.START";
    /**
     * {@link Intent} send by Orbot with {@code ON/OFF/STARTING/STOPPING} status
     */
    public final static String ACTION_STATUS = "org.torproject.android.intent.action.STATUS";
    /**
     * {@code String} that contains a status constant: {@link #STATUS_ON},
     * {@link #STATUS_OFF}, {@link #STATUS_STARTING}, or
     * {@link #STATUS_STOPPING}
     */
    public final static String EXTRA_STATUS = "org.torproject.android.intent.extra.STATUS";
    /**
     * A {@link String} {@code packageName} for Orbot to direct its status reply
     * to, used in {@link #ACTION_START} {@link Intent}s sent to Orbot
     */
    public final static String EXTRA_PACKAGE_NAME = "org.torproject.android.intent.extra.PACKAGE_NAME";

    /**
     * All tor-related services and daemons are stopped
     */
    public final static String STATUS_OFF = "OFF";
    /**
     * All tor-related services and daemons have completed starting
     */
    public final static String STATUS_ON = "ON";
    public final static String STATUS_STARTING = "STARTING";
    public final static String STATUS_STOPPING = "STOPPING";
    /**
     * The user has disabled the ability for background starts triggered by
     * apps. Fallback to the old Intent that brings up Orbot.
     */
    public final static String STATUS_STARTS_DISABLED = "STARTS_DISABLED";

    public final static String ACTION_START_TOR = "org.torproject.android.START_TOR";
    public final static String ACTION_REQUEST_HS = "org.torproject.android.REQUEST_HS_PORT";
    public final static int START_TOR_RESULT = 0x048079234;
    public final static int HS_REQUEST_CODE = 9999;

    private final static String FDROID_PACKAGE_NAME = "org.fdroid.fdroid";
    private final static String PLAY_PACKAGE_NAME = "com.android.vending";

    private OrbotHelper() {
        // only static utility methods, do not instantiate
    }

    public static boolean isOrbotRunning(Context context) {
        int procId = TorServiceUtils.findProcessId(context);

        return (procId != -1);
    }

    public static boolean isOrbotInstalled(Context context) {
        return isAppInstalled(context, ORBOT_PACKAGE_NAME);
    }

    private static boolean isAppInstalled(Context context, String uri) {
        try {
            PackageManager pm = context.getPackageManager();
            pm.getPackageInfo(uri, PackageManager.GET_ACTIVITIES);
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    public static void requestHiddenServiceOnPort(Activity activity, int port) {
        Intent intent = new Intent(ACTION_REQUEST_HS);
        intent.setPackage(ORBOT_PACKAGE_NAME);
        intent.putExtra("hs_port", port);

        activity.startActivityForResult(intent, HS_REQUEST_CODE);
    }

    /**
     * First, checks whether Orbot is installed. If Orbot is installed, then a
     * broadcast {@link Intent} is sent to request Orbot to start transparently
     * in the background. When Orbot receives this {@code Intent}, it will
     * immediately reply to this all with its status via an
     * {@link #ACTION_STATUS} {@code Intent} that is broadcast to the
     * {@code packageName} of the provided {@link Context} (i.e.
     * {@link Context#getPackageName()}.
     *
     * @param context the app {@link Context} will receive the reply
     * @return whether the start request was sent to Orbot
     */
    public static boolean requestStartTor(Context context) {
        if (OrbotHelper.isOrbotInstalled(context)) {
            Log.i("OrbotHelper", "requestStartTor " + context.getPackageName());
            Intent intent = getOrbotStartIntent();
            intent.putExtra(EXTRA_PACKAGE_NAME, context.getPackageName());
            context.sendBroadcast(intent);
            return true;
        }
        return false;
    }

    public static Intent getOrbotStartIntent() {
        Intent intent = new Intent(ACTION_START);
        intent.setPackage(ORBOT_PACKAGE_NAME);
        return intent;
    }

    /**
     * First, checks whether Orbot is installed, then checks whether Orbot is
     * running. If Orbot is installed and not running, then an {@link Intent} is
     * sent to request Orbot to start, which will show the main Orbot screen.
     * The result will be returned in
     * {@link Activity#onActivityResult(int requestCode, int resultCode, Intent data)}
     * with a {@code requestCode} of {@link START_TOR_RESULT}
     *
     * @param activity the {@link Activity} that gets the
     *            {@code START_TOR_RESULT} result
     * @return whether the start request was sent to Orbot
     */
    public static boolean requestShowOrbotStart(Activity activity) {
        if (OrbotHelper.isOrbotInstalled(activity)) {
            if (!OrbotHelper.isOrbotRunning(activity)) {
                Intent intent = getShowOrbotStartIntent();
                activity.startActivityForResult(intent, START_TOR_RESULT);
                return true;
            }
        }
        return false;
    }

    public static Intent getShowOrbotStartIntent() {
        Intent intent = new Intent(ACTION_START_TOR);
        intent.setPackage(ORBOT_PACKAGE_NAME);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        return intent;
    }

    public static Intent getOrbotInstallIntent(Context context) {
        final Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setData(Uri.parse(ORBOT_MARKET_URI));

        PackageManager pm = context.getPackageManager();
        List<ResolveInfo> resInfos = pm.queryIntentActivities(intent, 0);

        String foundPackageName = null;
        for (ResolveInfo r : resInfos) {
            Log.i("OrbotHelper", "market: " + r.activityInfo.packageName);
            if (TextUtils.equals(r.activityInfo.packageName, FDROID_PACKAGE_NAME)
                    || TextUtils.equals(r.activityInfo.packageName, PLAY_PACKAGE_NAME)) {
                foundPackageName = r.activityInfo.packageName;
                break;
            }
        }

        if (foundPackageName == null) {
            intent.setData(Uri.parse(ORBOT_FDROID_URI));
        } else {
            intent.setPackage(foundPackageName);
        }
        return intent;
    }
}
