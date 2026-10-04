package com.heartsoul.recomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import java.util.Locale;

/**
 * The start screen: picture size, scaling and the bottom screen, then START
 * GAME. Drawn like the bottom screen (BottomScreenView): the game's fonts,
 * flat GBA colors, every size a whole multiple of one pixel scale s.
 *
 * Works with a controller (the d-pad chooses and changes, A changes or
 * starts, START starts from anywhere) and by touch. The cursor starts on
 * START GAME, so A plays straight away with the last choices.
 */
final class StartScreenView extends View {
    interface Listener {
        void onStartGame(Settings settings);
    }

    // Design size in GBA pixels that the scale is chosen from.
    private static final int DESIGN_WIDTH = 256;
    private static final int DESIGN_HEIGHT = 192;
    // The panels get no wider than this, in GBA pixels.
    private static final int MAX_CONTENT_WIDTH = 320;

    private static final int MARGIN = 6;
    private static final int GAP = 4;
    private static final int PAD_X = 6;
    private static final int PAD_Y = 3;

    private static final int ROW_PICTURE = 0;
    private static final int ROW_SCALE = 1;
    private static final int ROW_BOTTOM = 2;
    private static final int ROW_START = 3;
    private static final int ROW_COUNT = 4;

    // The bottom screen's palette.
    private static final int COLOR_BACKGROUND = 0xFF2E5070;
    private static final int COLOR_BACKGROUND_STRIPE = 0xFF2A4A68;
    private static final int COLOR_WINDOW = 0xFFF8F8F0;
    private static final int COLOR_WINDOW_BORDER = 0xFF404858;
    private static final int COLOR_DARK_WINDOW = 0xFF283040;
    private static final int COLOR_DARK_BORDER = 0xFF101820;
    private static final int COLOR_ACTIVE_WINDOW = 0xFFFFF0C0;
    private static final int COLOR_ACTIVE_BORDER = 0xFFE07828;
    private static final int COLOR_CURSOR = 0xFFF8D030;
    private static final int COLOR_START = 0xFF50B060;
    private static final int COLOR_START_DARK = 0xFF2C6034;
    private static final int COLOR_START_LIGHT = 0xFFB8E0BC;
    private static final int COLOR_SCREEN = 0xFF101010;
    private static final int COLOR_PICTURE = 0xFF58A060;
    private static final int COLOR_PICTURE_EDGE = 0xFF88D090;

    private static final int TEXT_DARK = 0xFF505058;
    private static final int SHADOW_DARK = 0xFFD0D0C8;
    private static final int TEXT_LIGHT = 0xFFF8F8F8;
    private static final int SHADOW_LIGHT = 0xFF606878;
    private static final int TEXT_ACCENT = 0xFFF8D058;
    private static final int SHADOW_ACCENT = 0xFF806020;

    // The game's own glyphs (GbaFont.glyphFor maps U+E000 + code).
    private static final char ARROW_UP = (char) (0xE000 + 0x79);
    private static final char ARROW_DOWN = (char) (0xE000 + 0x7A);
    private static final char ARROW_LEFT = (char) (0xE000 + 0x7B);
    private static final char ARROW_RIGHT = (char) (0xE000 + 0x7C);
    private static final char CURSOR = (char) (0xE000 + 0xEF);
    private static final char TIMES = (char) (0xE000 + 0xB9);

    private static final String[] PICTURE_NAMES = { "ORIGINAL", "WIDESCREEN", "ZOOMED OUT" };
    private static final String[] SCALE_NAMES = { "FIT", "PIXEL PERFECT", "STRETCH" };

    private final Settings settings;
    private final int screenWidth;
    private final int screenHeight;
    private final boolean hasSecondScreen;
    private final String version;
    private final Listener listener;
    private final GbaText gba;
    private final Paint fill = new Paint();
    private final Paint fallbackText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF[] rowRects = new RectF[ROW_COUNT];
    private final int[] valueLeft = new int[ROW_COUNT];

    private int row = ROW_START;
    private int pressedRow = -1;
    private boolean started;
    /** The pixel scale of the current draw. */
    private int s = 1;

    StartScreenView(Context context, Settings settings, int screenWidth, int screenHeight,
                    boolean hasSecondScreen, String version, Listener listener) {
        super(context);
        this.settings = settings;
        this.screenWidth = screenWidth;
        this.screenHeight = screenHeight;
        this.hasSecondScreen = hasSecondScreen;
        this.version = version;
        this.listener = listener;
        for (int i = 0; i < ROW_COUNT; i++) {
            rowRects[i] = new RectF();
        }
        fill.setAntiAlias(false);
        gba = GbaText.load(context.getAssets());
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    // ------------------------------------------------------------------
    // Drawing
    // ------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        int width = getWidth();
        int height = getHeight();
        s = Math.max(1, Math.min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT));
        drawBackground(canvas, width, height);

        if (gba == null) {
            fallbackText.setColor(TEXT_LIGHT);
            fallbackText.setTextAlign(Paint.Align.CENTER);
            fallbackText.setTextSize(height / 20f);
            canvas.drawText("Font assets missing from the APK. Press A to play.", width / 2f, height / 2f,
                    fallbackText);
            return;
        }

        int margin = MARGIN * s;
        int gap = GAP * s;
        int contentWidth = Math.min(width - 2 * margin, MAX_CONTENT_WIDTH * s);
        int left = (width - contentWidth) / 2;
        int right = left + contentWidth;

        int mainLine = gba.main[0].boxHeight * s;
        int smallLine = gba.small[0].boxHeight * s;
        int titleHeight = 2 * mainLine + 2 * (PAD_Y + 1) * s;
        int optionRow = mainLine + 2 * PAD_Y * s;
        int optionsHeight = 3 * optionRow + 2 * (PAD_Y + 1) * s;
        int infoHeight = 2 * smallLine + 3 * PAD_Y * s + 2 * s;
        int startHeight = mainLine + 2 * (PAD_Y + 1) * s + 2 * s;
        int footerHeight = smallLine;
        int total = titleHeight + optionsHeight + infoHeight + startHeight + footerHeight + 4 * gap;

        // Spare height goes around the block, so it sits in the middle.
        int top = Math.max(margin, (height - total) / 2);

        drawTitle(canvas, left, top, right, top + titleHeight);
        top += titleHeight + gap;
        drawOptions(canvas, left, top, right, top + optionsHeight, optionRow);
        top += optionsHeight + gap;
        drawInfo(canvas, left, top, right, top + infoHeight);
        top += infoHeight + gap;
        drawStartButton(canvas, left, top, right, top + startHeight);
        top += startHeight + gap;
        drawFooter(canvas, left, top, right, top + footerHeight);
    }

    private void drawBackground(Canvas canvas, int width, int height) {
        canvas.drawColor(COLOR_BACKGROUND);
        int stripe = 2 * s;
        for (int y = stripe; y < height; y += 2 * stripe) {
            rect(canvas, 0, y, width, Math.min(height, y + stripe), COLOR_BACKGROUND_STRIPE);
        }
    }

    private void drawTitle(Canvas canvas, int left, int top, int right, int bottom) {
        window(canvas, left, top, right, bottom, COLOR_DARK_WINDOW, COLOR_DARK_BORDER);
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int center = (top + bottom) / 2;
        GbaText.Fit versionFit = GbaText.fit(gba.small, version, (inRight - inLeft) / 4, bottom - top, s, s, 1);
        GbaText.drawCentered(canvas, versionFit, inRight - versionFit.width, center, TEXT_LIGHT, SHADOW_LIGHT);
        GbaText.Fit title = GbaText.fit(gba.main, "POKéMON HEART & SOUL",
                inRight - inLeft - versionFit.width - GAP * s, bottom - top - 2 * s, 2 * s, s, s);
        GbaText.drawCentered(canvas, title, inLeft, center, TEXT_ACCENT, SHADOW_ACCENT);
    }

    private void drawOptions(Canvas canvas, int left, int top, int right, int bottom, int optionRow) {
        window(canvas, left, top, right, bottom, COLOR_WINDOW, COLOR_WINDOW_BORDER);
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int y = top + (PAD_Y + 1) * s;
        String[] labels = { "PICTURE", "SCALING", "BOTTOM SCREEN" };
        String[] values = {
            PICTURE_NAMES[settings.picture],
            SCALE_NAMES[settings.scale],
            settings.bottomScreen ? "ON" : "OFF",
        };
        // Labels share a size, and so do the values, so the rows line up.
        int labelRoom = (inRight - inLeft) * 45 / 100;
        int labelScale = GbaText.groupScale(gba.main, labels, labelRoom, optionRow, s, s, Math.max(1, s - 1));
        int cursorWidth = gba.main[0].width(String.valueOf(CURSOR)) * s + 2 * s;
        for (int i = 0; i < 3; i++) {
            boolean selected = row == i;
            rowRects[i].set(left, y, right, y + optionRow);
            if (selected) {
                rect(canvas, left + s, y, right - s, y + optionRow, COLOR_ACTIVE_WINDOW);
                rect(canvas, left + s, y + optionRow - s, right - s, y + optionRow, COLOR_ACTIVE_BORDER);
            }
            int center = y + optionRow / 2;
            if (selected) {
                GbaText.Fit cursor = GbaText.fit(gba.main, String.valueOf(CURSOR), cursorWidth, optionRow, s, s, 1);
                GbaText.drawCentered(canvas, cursor, inLeft, center, TEXT_DARK, SHADOW_DARK);
            }
            GbaText.Fit label = GbaText.fit(gba.main, labels[i], labelRoom - cursorWidth, optionRow,
                    labelScale, s, 1);
            GbaText.drawCentered(canvas, label, inLeft + cursorWidth, center, TEXT_DARK, SHADOW_DARK);

            // "< VALUE >": the arrows sit at fixed places, the value centred
            // between them.
            int arrowWidth = gba.main[0].width(String.valueOf(ARROW_LEFT)) * s;
            int valueRight = inRight;
            int valueStart = inLeft + labelRoom;
            valueLeft[i] = valueStart;
            if (selected) {
                GbaText.Fit leftArrow = GbaText.fit(gba.main, String.valueOf(ARROW_LEFT), arrowWidth, optionRow, s, s, 1);
                GbaText.Fit rightArrow = GbaText.fit(gba.main, String.valueOf(ARROW_RIGHT), arrowWidth, optionRow, s, s, 1);
                GbaText.drawCentered(canvas, leftArrow, valueStart, center, COLOR_ACTIVE_BORDER, SHADOW_DARK);
                GbaText.drawCentered(canvas, rightArrow, valueRight - rightArrow.width, center, COLOR_ACTIVE_BORDER,
                        SHADOW_DARK);
            }
            int room = valueRight - valueStart - 2 * (arrowWidth + 2 * s);
            GbaText.Fit value = GbaText.fit(gba.main, values[i], room, optionRow, s, s, Math.max(1, s - 1));
            int valueCenter = (valueStart + valueRight) / 2;
            GbaText.drawCentered(canvas, value, valueCenter - value.width / 2, center, TEXT_DARK, SHADOW_DARK);
            y += optionRow;
        }
    }

    /**
     * What the selected choice does, on the left, and on the right a small
     * picture of this screen with the game's picture placed in it as it
     * will be.
     */
    private void drawInfo(Canvas canvas, int left, int top, int right, int bottom) {
        window(canvas, left, top, right, bottom, COLOR_DARK_WINDOW, COLOR_DARK_BORDER);
        int inTop = top + (PAD_Y + 1) * s;
        int inBottom = bottom - (PAD_Y + 1) * s;
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;

        // The preview keeps this screen's shape.
        int previewHeight = inBottom - inTop;
        int previewWidth = screenHeight > 0 ? previewHeight * screenWidth / screenHeight : previewHeight * 4 / 3;
        int previewRight = inRight;
        int previewLeft = previewRight - previewWidth;
        drawPreview(canvas, previewLeft, inTop, previewRight, inBottom);

        int textRight = previewLeft - GAP * s;
        int smallLine = gba.small[0].boxHeight * s;
        String[] lines = describe();
        int lineGap = (inBottom - inTop - 2 * smallLine) / 3;
        int y = inTop + lineGap + smallLine / 2;
        for (String line : lines) {
            GbaText.Fit fit = GbaText.fit(gba.small, line, textRight - inLeft, smallLine, s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, fit, inLeft, y, TEXT_LIGHT, SHADOW_LIGHT);
            y += smallLine + lineGap;
        }
    }

    private void drawPreview(Canvas canvas, int left, int top, int right, int bottom) {
        rect(canvas, left, top, right, bottom, COLOR_WINDOW_BORDER);
        int inLeft = left + s;
        int inTop = top + s;
        int inRight = right - s;
        int inBottom = bottom - s;
        rect(canvas, inLeft, inTop, inRight, inBottom, COLOR_SCREEN);
        if (screenWidth <= 0 || screenHeight <= 0) {
            return;
        }
        int[] picture = pictureSize();
        float ratio = (float) (inRight - inLeft) / screenWidth;
        int w = Math.round(picture[0] * ratio);
        int h = Math.round(picture[1] * ratio);
        int x = inLeft + (inRight - inLeft - w) / 2;
        int y = inTop + (inBottom - inTop - h) / 2;
        rect(canvas, x, y, x + w, y + h, COLOR_PICTURE_EDGE);
        rect(canvas, x + 1, y + 1, x + w - 1, y + h - 1, COLOR_PICTURE);
    }

    private void drawStartButton(Canvas canvas, int left, int top, int right, int bottom) {
        boolean selected = row == ROW_START;
        boolean pressed = pressedRow == ROW_START;
        rowRects[ROW_START].set(left, top, right, bottom);
        int inset = 2 * s;
        if (selected) {
            window(canvas, left, top, right, bottom, COLOR_CURSOR, COLOR_CURSOR);
        }
        int bLeft = left + inset;
        int bTop = top + inset;
        int bRight = right - inset;
        int bBottom = bottom - inset;
        window(canvas, bLeft, bTop, bRight, bBottom, pressed ? COLOR_START : COLOR_START_LIGHT, COLOR_START_DARK);
        if (!pressed) {
            rect(canvas, bLeft + s, bBottom - 3 * s, bRight - s, bBottom - s, COLOR_START);
        }
        GbaText.Fit label = GbaText.fit(gba.main, "START GAME", bRight - bLeft - 4 * s, bBottom - bTop, 2 * s, s, s);
        GbaText.drawCentered(canvas, label, (bLeft + bRight - label.width) / 2, (bTop + bBottom - 2 * s) / 2,
                TEXT_DARK, SHADOW_DARK);
    }

    private void drawFooter(Canvas canvas, int left, int top, int right, int bottom) {
        String hint = ARROW_UP + "" + ARROW_DOWN + " CHOOSE   " + ARROW_LEFT + ARROW_RIGHT
                + " CHANGE   A OK   START PLAY";
        GbaText.Fit fit = GbaText.fit(gba.small, hint, right - left, bottom - top + 2 * s, s, s, 1);
        GbaText.drawCentered(canvas, fit, (left + right - fit.width) / 2, (top + bottom) / 2, TEXT_LIGHT,
                SHADOW_LIGHT);
    }

    /** Two lines about the selected row. The second is the numbers. */
    private String[] describe() {
        String first;
        switch (row) {
            case ROW_PICTURE:
                switch (settings.picture) {
                    case Settings.PICTURE_ORIGINAL:
                        first = "The GBA picture, nothing added.";
                        break;
                    case Settings.PICTURE_WIDESCREEN:
                        first = "More map to the left and right.";
                        break;
                    default:
                        first = "More map on all four sides. Menus stay GBA size.";
                        break;
                }
                break;
            case ROW_SCALE:
                switch (settings.scale) {
                    case Settings.SCALE_FIT:
                        first = "As big as fits, same shape.";
                        break;
                    case Settings.SCALE_INTEGER:
                        first = "Whole-number zoom: every pixel the same size.";
                        break;
                    default:
                        first = "Fills the screen, changes the shape.";
                        break;
                }
                break;
            case ROW_BOTTOM:
                if (!hasSecondScreen) {
                    first = "No second screen found on this device.";
                } else if (settings.bottomScreen) {
                    first = "Party and battle buttons on the second screen.";
                } else {
                    first = "Leaves the second screen to other apps.";
                }
                break;
            default:
                first = "Changes apply when the game starts.";
                break;
        }
        return new String[] { first, numbers() };
    }

    /** "640×480 SCREEN, 288×216 PICTURE AT 2×" and the like. */
    private String numbers() {
        int fw = Settings.frameWidth(settings.picture);
        int fh = Settings.frameHeight(settings.picture);
        String frame = fw + "" + TIMES + fh;
        if (screenWidth <= 0 || screenHeight <= 0) {
            return frame + " PICTURE";
        }
        String screen = screenWidth + "" + TIMES + screenHeight;
        String zoom;
        if (settings.scale == Settings.SCALE_STRETCH) {
            zoom = "STRETCHED";
        } else {
            float scale = Math.min((float) screenWidth / fw, (float) screenHeight / fh);
            if (settings.scale == Settings.SCALE_INTEGER) {
                zoom = "AT " + Math.max(1, (int) scale) + TIMES;
            } else {
                zoom = String.format(Locale.US, "AT %.1f", scale) + TIMES;
            }
        }
        return frame + " ON " + screen + ", " + zoom;
    }

    /** The picture's size on this screen in screen pixels, as SDL will place it. */
    private int[] pictureSize() {
        int fw = Settings.frameWidth(settings.picture);
        int fh = Settings.frameHeight(settings.picture);
        if (settings.scale == Settings.SCALE_STRETCH) {
            return new int[] { screenWidth, screenHeight };
        }
        float scale = Math.min((float) screenWidth / fw, (float) screenHeight / fh);
        if (settings.scale == Settings.SCALE_INTEGER) {
            scale = Math.max(1, (int) scale);
        }
        return new int[] { Math.round(fw * scale), Math.round(fh * scale) };
    }

    private void window(Canvas canvas, int left, int top, int right, int bottom, int inside, int border) {
        if (right - left < 3 * s || bottom - top < 3 * s) {
            rect(canvas, left, top, right, bottom, border);
            return;
        }
        rect(canvas, left + s, top, right - s, bottom, border);
        rect(canvas, left, top + s, right, bottom - s, border);
        rect(canvas, left + s, top + s, right - s, bottom - s, inside);
    }

    private void rect(Canvas canvas, int left, int top, int right, int bottom, int color) {
        fill.setColor(color);
        canvas.drawRect(left, top, right, bottom, fill);
    }

    // ------------------------------------------------------------------
    // Input
    // ------------------------------------------------------------------

    private void change(int direction) {
        switch (row) {
            case ROW_PICTURE:
                settings.picture = (settings.picture + direction + Settings.PICTURE_COUNT) % Settings.PICTURE_COUNT;
                break;
            case ROW_SCALE:
                settings.scale = (settings.scale + direction + Settings.SCALE_COUNT) % Settings.SCALE_COUNT;
                break;
            case ROW_BOTTOM:
                settings.bottomScreen = !settings.bottomScreen;
                break;
            default:
                return;
        }
        settings.save(getContext());
        invalidate();
    }

    private void start() {
        if (started) {
            return;
        }
        started = true;
        listener.onStartGame(settings);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        boolean repeat = event.getRepeatCount() > 0;
        switch (keyCode) {
            case KeyEvent.KEYCODE_DPAD_UP:
                row = (row + ROW_COUNT - 1) % ROW_COUNT;
                invalidate();
                return true;
            case KeyEvent.KEYCODE_DPAD_DOWN:
                row = (row + 1) % ROW_COUNT;
                invalidate();
                return true;
            case KeyEvent.KEYCODE_DPAD_LEFT:
                change(-1);
                return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT:
                change(1);
                return true;
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_DPAD_CENTER:
            case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_NUMPAD_ENTER:
                if (!repeat) {
                    if (row == ROW_START) {
                        start();
                    } else {
                        change(1);
                    }
                }
                return true;
            case KeyEvent.KEYCODE_BUTTON_START:
                if (!repeat) {
                    start();
                }
                return true;
            case KeyEvent.KEYCODE_BUTTON_B:
                // Handled so Android does not turn it into Back and close the
                // app on a pad whose A and B are the other way round.
                row = ROW_START;
                invalidate();
                return true;
            default:
                return super.onKeyDown(keyCode, event);
        }
    }

    private int rowAt(float x, float y) {
        for (int i = 0; i < ROW_COUNT; i++) {
            if (rowRects[i].contains(x, y)) {
                return i;
            }
        }
        return -1;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pressedRow = rowAt(event.getX(), event.getY());
                if (pressedRow >= 0) {
                    row = pressedRow;
                }
                invalidate();
                return true;
            case MotionEvent.ACTION_UP: {
                int released = rowAt(event.getX(), event.getY());
                int was = pressedRow;
                pressedRow = -1;
                if (released >= 0 && released == was) {
                    if (released == ROW_START) {
                        start();
                    } else {
                        // The left half of the value goes back, anything else forward.
                        RectF r = rowRects[released];
                        int middle = (valueLeft[released] + (int) r.right) / 2;
                        boolean back = event.getX() >= valueLeft[released] && event.getX() < middle;
                        change(back ? -1 : 1);
                    }
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                pressedRow = -1;
                invalidate();
                return true;
            default:
                return true;
        }
    }
}
