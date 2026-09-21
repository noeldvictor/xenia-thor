package jp.xenia.emulator;

import android.graphics.Bitmap;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Screen-driven routing for the debug server (2026-09-21).
 *
 * A timed button route ("START at 40 s") lands on a transition as often as
 * on the screen, because the load time varies run to run. This drives the
 * route by what is on the panel: a step waits until a predicate over the
 * frame statistics holds, then presses a button, then settles. The stats are
 * cheap (a sampled grid of the PixelCopy bitmap) and generic: mean luma of
 * the frame and of its halves, black fractions, a saturated-hue histogram.
 * Presets name the screens of a title; a client can also pass raw steps.
 *
 * Predicate syntax: "gold>0.35&lower_black<0.1" (keys below, ops < and >).
 * Steps syntax: "until:gold>0.35;press:START;settle:1500|until:lower_black>0.4"
 */
final class ScreenRoutes {
    static final class Stats {
        int width, height;
        double luma, topLuma, bottomLuma;
        double black, lowerBlack, upperBlack;
        double sat;      // fraction of saturated pixels
        double[] hue = new double[6];  // saturated pixels by hue sextant: red, yellow, green, cyan, blue, magenta
        double gold;     // saturated warm pixels (red..yellow, bright): the Banjo puzzle
        long swaps;

        double get(final String key) {
            switch (key) {
                case "luma": return luma;
                case "top_luma": return topLuma;
                case "bottom_luma": return bottomLuma;
                case "black": return black;
                case "lower_black": return lowerBlack;
                case "upper_black": return upperBlack;
                case "sat": return sat;
                case "gold": return gold;
                case "red": return hue[0];
                case "yellow": return hue[1];
                case "green": return hue[2];
                case "cyan": return hue[3];
                case "blue": return hue[4];
                case "magenta": return hue[5];
                case "swaps": return swaps;
                default: return Double.NaN;
            }
        }

        String toJson() {
            return String.format(Locale.US,
                    "{\"width\":%d,\"height\":%d,\"luma\":%.1f,\"top_luma\":%.1f,\"bottom_luma\":%.1f,"
                            + "\"black\":%.3f,\"lower_black\":%.3f,\"upper_black\":%.3f,\"sat\":%.3f,\"gold\":%.3f,"
                            + "\"red\":%.3f,\"yellow\":%.3f,\"green\":%.3f,\"cyan\":%.3f,\"blue\":%.3f,\"magenta\":%.3f,"
                            + "\"swaps\":%d}",
                    width, height, luma, topLuma, bottomLuma, black, lowerBlack, upperBlack, sat, gold,
                    hue[0], hue[1], hue[2], hue[3], hue[4], hue[5], swaps);
        }
    }

    /** Statistics over a sampled grid of at most 192x108 points. */
    static Stats compute(final Bitmap bitmap, final long swaps) {
        final Stats s = new Stats();
        s.width = bitmap.getWidth();
        s.height = bitmap.getHeight();
        s.swaps = swaps;
        if (s.width == 0 || s.height == 0) {
            return s;
        }
        final int stepX = Math.max(1, s.width / 192);
        final int stepY = Math.max(1, s.height / 108);
        long lumaSum = 0, topSum = 0, bottomSum = 0;
        int n = 0, top = 0, bottom = 0, black = 0, upperBlack = 0, lowerBlack = 0, sat = 0, gold = 0;
        final int[] hue = new int[6];
        final int half = s.height / 2;
        for (int y = 0; y < s.height; y += stepY) {
            for (int x = 0; x < s.width; x += stepX) {
                final int c = bitmap.getPixel(x, y);
                final int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                final int l = (r * 299 + g * 587 + b * 114) / 1000;
                ++n;
                lumaSum += l;
                final boolean isBlack = l < 12;
                if (isBlack) {
                    ++black;
                }
                if (y < half) {
                    ++top;
                    topSum += l;
                    if (isBlack) {
                        ++upperBlack;
                    }
                } else {
                    ++bottom;
                    bottomSum += l;
                    if (isBlack) {
                        ++lowerBlack;
                    }
                }
                final int max = Math.max(r, Math.max(g, b)), min = Math.min(r, Math.min(g, b));
                if (max > 60 && max - min > 60) {
                    ++sat;
                    // Hue sextant from the dominant channel order.
                    int sextant;
                    if (max == r) {
                        sextant = g >= b ? (g - b > (max - min) / 2 ? 1 : 0) : 5;
                    } else if (max == g) {
                        sextant = b >= r ? 3 : 2;
                    } else {
                        sextant = r >= g ? 5 : 4;
                    }
                    ++hue[sextant];
                    if (max == r && g >= b && b < 90 && r > 140) {
                        ++gold;
                    }
                }
            }
        }
        s.luma = n > 0 ? (double) lumaSum / n : 0;
        s.topLuma = top > 0 ? (double) topSum / top : 0;
        s.bottomLuma = bottom > 0 ? (double) bottomSum / bottom : 0;
        s.black = n > 0 ? (double) black / n : 0;
        s.upperBlack = top > 0 ? (double) upperBlack / top : 0;
        s.lowerBlack = bottom > 0 ? (double) lowerBlack / bottom : 0;
        s.sat = n > 0 ? (double) sat / n : 0;
        s.gold = n > 0 ? (double) gold / n : 0;
        for (int i = 0; i < 6; ++i) {
            s.hue[i] = n > 0 ? (double) hue[i] / n : 0;
        }
        return s;
    }

    /** "gold>0.35&lower_black<0.1"; an empty predicate is true; unknown keys are false. */
    static boolean eval(final String predicate, final Stats s) {
        if (predicate == null || predicate.trim().isEmpty()) {
            return true;
        }
        for (final String term : predicate.split("&")) {
            final String t = term.trim();
            final int gt = t.indexOf('>'), lt = t.indexOf('<');
            final int at = gt >= 0 ? gt : lt;
            if (at <= 0) {
                return false;
            }
            final double value = s.get(t.substring(0, at).trim());
            final double limit;
            try {
                limit = Double.parseDouble(t.substring(at + 1).trim());
            } catch (NumberFormatException e) {
                return false;
            }
            if (Double.isNaN(value)) {
                return false;
            }
            if (gt >= 0 ? !(value > limit) : !(value < limit)) {
                return false;
            }
        }
        return true;
    }

    static final class Step {
        String name = "";
        String until = "";
        String press = "";
        int holdMs = 150;
        int settleMs = 1000;
        int timeoutS = 90;

        String toJson() {
            return String.format(Locale.US,
                    "{\"name\":\"%s\",\"until\":\"%s\",\"press\":\"%s\",\"hold_ms\":%d,\"settle_ms\":%d,\"timeout_s\":%d}",
                    DebugServer.jsonEscape(name), DebugServer.jsonEscape(until), DebugServer.jsonEscape(press),
                    holdMs, settleMs, timeoutS);
        }
    }

    /** "until:gold>0.35;press:START;settle:1500;timeout:90|until:lower_black>0.4" */
    static List<Step> parse(final String steps) {
        final List<Step> out = new ArrayList<>();
        if (steps == null) {
            return out;
        }
        for (final String item : steps.split("\\|")) {
            if (item.trim().isEmpty()) {
                continue;
            }
            final Step step = new Step();
            for (final String field : item.split(";")) {
                final int colon = field.indexOf(':');
                if (colon <= 0) {
                    continue;
                }
                final String key = field.substring(0, colon).trim().toLowerCase(Locale.US);
                final String value = field.substring(colon + 1).trim();
                switch (key) {
                    case "name": step.name = value; break;
                    case "until": step.until = value; break;
                    case "press": step.press = value.toUpperCase(Locale.US); break;
                    case "hold": step.holdMs = Integer.parseInt(value); break;
                    case "settle": step.settleMs = Integer.parseInt(value); break;
                    case "timeout": step.timeoutS = Integer.parseInt(value); break;
                    default: break;
                }
            }
            out.add(step);
        }
        return out;
    }

    /**
     * The screens of a title by id. A screen is the steps that reach it from
     * a fresh launch; the last step's predicate is the screen itself.
     */
    static String preset(final String titleId, final String screen) {
        final String id = titleId == null ? "" : titleId.toUpperCase(Locale.US);
        final String sc = screen == null ? "" : screen.toLowerCase(Locale.US);
        if ("4D5307ED".equals(id)) {  // Banjo-Kazooie: Nuts & Bolts
            // The gold puzzle is the title screen; START opens the main menu,
            // whose lower half is black on the device (the open glitch); A on
            // "Single Player" loads the world (the opening story text box).
            final String title = "name:title;until:gold>0.35;timeout:120";
            final String menu = title + ";press:START;settle:2500|name:menu;until:lower_black>0.4&gold<0.3;timeout:60";
            switch (sc) {
                case "title": return title;
                case "menu": return menu;
                case "world": return menu + ";press:A;settle:3000|name:world;until:lower_black<0.2&gold<0.2;timeout:120";
                default: return null;
            }
        }
        return null;
    }
}
