package jp.xenia.emulator;

import android.content.Context;
import android.content.SharedPreferences;
import android.view.KeyEvent;

public final class XeniaInputMapping {
    private static final String KEY_PREFIX = "controller_mapping_";

    public static final class ButtonAction {
        public final String id;
        public final String label;
        public final String xboxControl;
        public final int targetKeyCode;
        public final int defaultPhysicalKeyCode;

        private ButtonAction(
                final String id,
                final String label,
                final String xboxControl,
                final int targetKeyCode,
                final int defaultPhysicalKeyCode) {
            this.id = id;
            this.label = label;
            this.xboxControl = xboxControl;
            this.targetKeyCode = targetKeyCode;
            this.defaultPhysicalKeyCode = defaultPhysicalKeyCode;
        }
    }

    private static final ButtonAction[] BUTTON_ACTIONS = new ButtonAction[] {
            action("dpad_up", "D-pad Up", "XInput D-pad Up",
                    KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_UP),
            action("dpad_down", "D-pad Down", "XInput D-pad Down",
                    KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_DOWN),
            action("dpad_left", "D-pad Left", "XInput D-pad Left",
                    KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_LEFT),
            action("dpad_right", "D-pad Right", "XInput D-pad Right",
                    KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_RIGHT),
            action("a", "A", "XInput A",
                    KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_A),
            action("b", "B", "XInput B",
                    KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_B),
            action("x", "X", "XInput X",
                    KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_X),
            action("y", "Y", "XInput Y",
                    KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_Y),
            action("lb", "LB", "XInput Left Shoulder",
                    KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_L1),
            action("rb", "RB", "XInput Right Shoulder",
                    KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_R1),
            action("lt", "LT", "XInput Left Trigger",
                    KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_L2),
            action("rt", "RT", "XInput Right Trigger",
                    KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_BUTTON_R2),
            action("ls", "LS", "XInput Left Stick Click",
                    KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBL),
            action("rs", "RS", "XInput Right Stick Click",
                    KeyEvent.KEYCODE_BUTTON_THUMBR, KeyEvent.KEYCODE_BUTTON_THUMBR),
            action("back", "Back", "XInput Back",
                    KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_SELECT),
            action("start", "Start", "XInput Start",
                    KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_START),
            action("guide", "Guide", "XInput Guide",
                    KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_BUTTON_MODE),
    };

    private XeniaInputMapping() {
    }

    public static ButtonAction[] getButtonActions() {
        return BUTTON_ACTIONS.clone();
    }

    public static ButtonAction findAction(final String actionId) {
        for (final ButtonAction action : BUTTON_ACTIONS) {
            if (action.id.equals(actionId)) {
                return action;
            }
        }
        return null;
    }

    public static boolean isBindableKeyCode(final int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_DPAD_UP:
            case KeyEvent.KEYCODE_DPAD_DOWN:
            case KeyEvent.KEYCODE_DPAD_LEFT:
            case KeyEvent.KEYCODE_DPAD_RIGHT:
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_BUTTON_B:
            case KeyEvent.KEYCODE_BUTTON_C:
            case KeyEvent.KEYCODE_BUTTON_X:
            case KeyEvent.KEYCODE_BUTTON_Y:
            case KeyEvent.KEYCODE_BUTTON_Z:
            case KeyEvent.KEYCODE_BUTTON_L1:
            case KeyEvent.KEYCODE_BUTTON_R1:
            case KeyEvent.KEYCODE_BUTTON_L2:
            case KeyEvent.KEYCODE_BUTTON_R2:
            case KeyEvent.KEYCODE_BUTTON_THUMBL:
            case KeyEvent.KEYCODE_BUTTON_THUMBR:
            case KeyEvent.KEYCODE_BUTTON_START:
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_BUTTON_MODE:
                return true;
            default:
                return false;
        }
    }

    public static int mapAndroidKeyCode(final Context context, final int physicalKeyCode) {
        if (!isBindableKeyCode(physicalKeyCode)) {
            return physicalKeyCode;
        }
        final SharedPreferences preferences = XeniaAndroidSettings.getPreferences(context);
        for (final ButtonAction action : BUTTON_ACTIONS) {
            if (getPhysicalKeyCode(preferences, action) == physicalKeyCode) {
                return action.targetKeyCode;
            }
        }
        return physicalKeyCode;
    }

    public static int getPhysicalKeyCode(final Context context, final ButtonAction action) {
        return getPhysicalKeyCode(XeniaAndroidSettings.getPreferences(context), action);
    }

    public static void setPhysicalKeyForAction(
            final Context context, final String actionId, final int physicalKeyCode) {
        final SharedPreferences preferences = XeniaAndroidSettings.getPreferences(context);
        final ButtonAction targetAction = findAction(actionId);
        if (targetAction == null || !isBindableKeyCode(physicalKeyCode)) {
            return;
        }
        final int previousPhysicalKeyCode = getPhysicalKeyCode(preferences, targetAction);
        final SharedPreferences.Editor editor = preferences.edit();
        for (final ButtonAction action : BUTTON_ACTIONS) {
            if (!action.id.equals(actionId)
                    && getPhysicalKeyCode(preferences, action) == physicalKeyCode) {
                editor.putInt(keyForAction(action), previousPhysicalKeyCode);
            }
        }
        editor.putInt(keyForAction(targetAction), physicalKeyCode);
        editor.apply();
    }

    public static void resetToDefaults(final Context context) {
        final SharedPreferences.Editor editor = XeniaAndroidSettings.getPreferences(context).edit();
        for (final ButtonAction action : BUTTON_ACTIONS) {
            editor.putInt(keyForAction(action), action.defaultPhysicalKeyCode);
        }
        editor.apply();
    }

    public static String keyName(final int keyCode) {
        String name = KeyEvent.keyCodeToString(keyCode);
        if (name == null || name.length() == 0) {
            return "KEYCODE_" + keyCode;
        }
        if (name.startsWith("KEYCODE_")) {
            name = name.substring("KEYCODE_".length());
        }
        if (name.startsWith("BUTTON_")) {
            name = name.substring("BUTTON_".length());
        }
        return name.replace('_', ' ');
    }

    private static ButtonAction action(
            final String id,
            final String label,
            final String xboxControl,
            final int targetKeyCode,
            final int defaultPhysicalKeyCode) {
        return new ButtonAction(id, label, xboxControl, targetKeyCode, defaultPhysicalKeyCode);
    }

    private static int getPhysicalKeyCode(
            final SharedPreferences preferences, final ButtonAction action) {
        return preferences.getInt(keyForAction(action), action.defaultPhysicalKeyCode);
    }

    private static String keyForAction(final ButtonAction action) {
        return KEY_PREFIX + action.id;
    }
}
