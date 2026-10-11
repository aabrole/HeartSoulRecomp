package com.heartsoul.recomp;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Draws the companion screen: a status line (player, place, money, badges,
 * play time, fast forward), a row of tabs, and the open tab's page.
 *
 * PARTY: the six party cards with their menu icons; touch one for its
 * summary (types, ability, nature, stats with IVs and EVs, moves with power
 * and accuracy, and what it is weak or resistant to).
 * BATTLE: opens by itself when a battle starts. The foe with its types and,
 * against a trainer, how many of the trainer's Pokémon are left; the four
 * moves as touch buttons, each marked SUPER, RESISTED or NO EFFECT against
 * the foe's types; and Bag / Pokémon / Run.
 * BAG: the pockets, their items, and the touched item's icon and text.
 *
 * A touch on a battle button only sends a request to the game. What is on
 * screen changes when the next snapshot says the game did.
 *
 * Everything is drawn GBA style: text in the game's own fonts (GbaText),
 * flat colors, borders one scaled pixel thick. One number sets every size:
 * the pixel scale s, the largest whole scale at which a 300x240 pixel
 * design fits the view (4 on the Thor's 1240x1080 bottom screen, 2 on the
 * RG DS's 640x480). Margins, gaps, borders and text are whole multiples of
 * s; panels share out what is left of the view, so the layout fills any
 * size. Each string is fitted to the box it was given (see GbaText), so no
 * text overlaps or leaves its box.
 */
final class BottomScreenView extends View {
    // Design size in GBA pixels that the scale is chosen from.
    private static final int DESIGN_WIDTH = 300;
    private static final int DESIGN_HEIGHT = 240;

    // Layout, in GBA pixels (multiplied by the scale s).
    private static final int MARGIN = 4;
    private static final int GAP = 3;
    private static final int PAD_X = 4;
    private static final int PAD_Y = 2;

    // Tabs.
    private static final int TAB_PARTY = 0;
    private static final int TAB_BATTLE = 1;
    private static final int TAB_BAG = 2;
    private static final String[] TAB_NAMES = { "PARTY", "BATTLE", "BAG" };

    // Touch targets handled here rather than sent to the game. The battle
    // buttons use DualScreenBridge.TAP_MOVE and TAP_ACTION.
    private static final int UI_TAB = 100;
    private static final int UI_MON = 101;
    private static final int UI_BACK = 102;
    private static final int UI_PREVIOUS_MON = 103;
    private static final int UI_NEXT_MON = 104;
    private static final int UI_POCKET = 105;
    private static final int UI_ITEM = 106;
    private static final int UI_SCROLL = 107; // index 0 up, 1 down
    private static final int UI_MATCHUPS = 108; // index 1 opens them, 0 goes back

    // Palette. Light windows with dark text, as in the game's menus.
    private static final int COLOR_BACKGROUND = 0xFF2E5070;
    private static final int COLOR_BACKGROUND_STRIPE = 0xFF2A4A68;
    private static final int COLOR_WINDOW = 0xFFF8F8F0;
    private static final int COLOR_WINDOW_BORDER = 0xFF404858;
    private static final int COLOR_DARK_WINDOW = 0xFF283040;
    private static final int COLOR_DARK_BORDER = 0xFF101820;
    private static final int COLOR_ACTIVE_WINDOW = 0xFFFFF0C0;
    private static final int COLOR_ACTIVE_BORDER = 0xFFE07828;
    private static final int COLOR_FAINTED_WINDOW = 0xFFF0D0C8;
    private static final int COLOR_EMPTY_WINDOW = 0xFF284660;
    private static final int COLOR_EMPTY_BORDER = 0xFF3C6488;
    private static final int COLOR_CURSOR = 0xFFF8D030;
    private static final int COLOR_ROW_SELECTED = 0xFFFFE8A8;

    private static final int TEXT_DARK = 0xFF505058;
    private static final int SHADOW_DARK = 0xFFD0D0C8;
    private static final int TEXT_DIM = 0xFF989898;
    private static final int SHADOW_DIM = 0xFFE0E0D8;
    private static final int TEXT_LIGHT = 0xFFF8F8F8;
    private static final int SHADOW_LIGHT = 0xFF606878;
    private static final int TEXT_ACCENT = 0xFFF8D058;
    private static final int SHADOW_ACCENT = 0xFF806020;
    // The summary screen's colors for the stat a nature raises and lowers.
    private static final int TEXT_RAISED = 0xFFE03820;
    private static final int SHADOW_RAISED = 0xFFF8C0B0;
    private static final int TEXT_LOWERED = 0xFF3060D0;
    private static final int SHADOW_LOWERED = 0xFFB8D0F8;

    private static final int HP_FRAME = 0xFF404858;
    private static final int HP_TRACK = 0xFF586070;
    private static final int[] HP_GOOD = { 0xFF58D080, 0xFF88F8B0 };
    private static final int[] HP_LOW = { 0xFFE8B020, 0xFFF8E070 };
    private static final int[] HP_CRITICAL = { 0xFFE84830, 0xFFF89070 };
    private static final int[] EXP_FILL = { 0xFF40A8F8, 0xFF88D8F8 };

    private static final int COLOR_BAG = 0xFFE8A030;
    private static final int COLOR_POKEMON = 0xFF50B060;
    private static final int COLOR_RUN = 0xFF4888E0;
    private static final int COLOR_FAST_FORWARD = 0xFFE07828;

    private static final int COLOR_SUPER = 0xFF48A848;
    private static final int COLOR_RESISTED = 0xFFC07838;
    private static final int COLOR_IMMUNE = 0xFF707880;

    // Arrows from the game's font, by character code.
    private static final String ARROW_UP = String.valueOf((char) (0xE000 + 0x79));
    private static final String ARROW_DOWN = String.valueOf((char) (0xE000 + 0x7A));
    private static final String ARROW_LEFT = String.valueOf((char) (0xE000 + 0x7B));
    private static final String ARROW_RIGHT = String.valueOf((char) (0xE000 + 0x7C));

    private static final String[] STAT_NAMES = { "HP", "ATTACK", "DEFENSE", "SP. ATK", "SP. DEF", "SPEED" };
    // Pocket labels short enough for six across; the game's names otherwise.
    private static final String[] POCKET_SHORT_NAMES = { "ITEMS", "MEDICINE", "BALLS", "TM/HM", "BERRIES", "KEY" };

    /** A touch target laid out by the last draw. */
    private static final class Button {
        final RectF rect = new RectF();
        int kind;
        int index;
        boolean enabled;
    }

    private final Paint fill = new Paint();
    private final Paint pixels = new Paint();
    private final Paint fallbackText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final List<Button> buttons = new ArrayList<>();
    private final GbaText gba;
    private final IconCache icons = new IconCache();
    private final Rect iconSource = new Rect();
    private final Rect iconTarget = new Rect();

    private BottomScreenState state = new BottomScreenState();
    private TypeChart typeChart;
    private boolean typeChartTried;
    private BagState bag;
    private boolean bagWanted;
    private boolean bridgeBroken;

    private int tab = TAB_PARTY;
    private int tabBeforeBattle = TAB_PARTY;
    private boolean wasInBattle;
    /** The party slot whose summary is open, or -1. */
    private int detailSlot = -1;
    /** The summary shows the type matchups in place of the info. */
    private boolean summaryMatchups;
    private int pocket;
    private int bagSelected;
    private int bagScroll;
    /** Rows of the bag list in the last draw, and where the list was. */
    private int bagRows = 1;
    private final RectF bagListRect = new RectF();
    private int bagRowHeight = 1;

    private int pressedKind;
    private int pressedIndex;
    private float dragStartY;
    private int dragStartScroll;
    private boolean dragging;
    private boolean waitingForIcons;

    /** The pixel scale of the current draw. */
    private int s = 1;

    BottomScreenView(Context context) {
        super(context);
        fill.setAntiAlias(false);
        pixels.setAntiAlias(false);
        pixels.setFilterBitmap(false);
        gba = GbaText.load(context.getAssets());
    }

    void setState(BottomScreenState newState) {
        state = newState;
        // A battle opens its tab, and the tab that was open comes back after.
        if (state.inBattle && !wasInBattle) {
            tabBeforeBattle = tab == TAB_BATTLE ? TAB_PARTY : tab;
            tab = TAB_BATTLE;
        } else if (!state.inBattle && wasInBattle && tab == TAB_BATTLE) {
            tab = tabBeforeBattle;
        }
        wasInBattle = state.inBattle;
        if (detailSlot >= state.party.size()) {
            detailSlot = -1;
        }
        updateBag();
        invalidate();
    }

    /** Debug builds: once a second, saves what the screen shows if asked to. */
    private final Runnable dumpTick = new Runnable() {
        @Override
        public void run() {
            if (getWidth() > 0 && getHeight() > 0 && DebugFiles.flag(getContext(), "dump_bottom_screen")) {
                dump();
            }
            postDelayed(this, 1000);
        }
    };

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (DebugFiles.output(getContext(), "bottom_screen.png") != null) {
            post(dumpTick);
        }
    }

    private void dump() {
        File file = DebugFiles.output(getContext(), "bottom_screen.png");
        if (file == null) {
            return;
        }
        Bitmap bitmap = Bitmap.createBitmap(getWidth(), getHeight(), Bitmap.Config.ARGB_8888);
        draw(new Canvas(bitmap));
        try (FileOutputStream out = new FileOutputStream(file)) {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
        } catch (IOException e) {
            // Debug only; nothing to do.
        }
        bitmap.recycle();
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        removeCallbacks(dumpTick);
        setBagWanted(false);
    }

    // ------------------------------------------------------------------
    // Game data that is not in the snapshot
    // ------------------------------------------------------------------

    /** Asks for the bag while its tab is open, and picks up new ones. */
    private void updateBag() {
        boolean want = tab == TAB_BAG && state.inGame;
        setBagWanted(want);
        if (!want || bridgeBroken) {
            return;
        }
        try {
            String preview = DebugFiles.text(getContext(), "bottom_bag_preview.json");
            BagState fresh = BagState.parse(preview != null ? preview : DualScreenBridge.nativeGetBagJson());
            if (fresh != null && fresh.frame >= 0 && (bag == null || fresh.frame != bag.frame)) {
                bag = fresh;
            }
        } catch (UnsatisfiedLinkError e) {
            bridgeBroken = true;
        }
    }

    private void setBagWanted(boolean want) {
        if (want == bagWanted || bridgeBroken) {
            return;
        }
        bagWanted = want;
        try {
            DualScreenBridge.nativeSetBagWanted(want);
        } catch (UnsatisfiedLinkError e) {
            bridgeBroken = true;
        }
    }

    private TypeChart typeChart() {
        if (typeChart == null && !typeChartTried && !bridgeBroken) {
            typeChartTried = true;
            try {
                typeChart = TypeChart.parse(DualScreenBridge.nativeGetTypeChartJson());
            } catch (UnsatisfiedLinkError e) {
                bridgeBroken = true;
            }
        }
        return typeChart;
    }

    // ------------------------------------------------------------------
    // Layout
    // ------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        int width = getWidth();
        int height = getHeight();
        s = Math.max(1, Math.min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT));
        buttons.clear();
        waitingForIcons = false;
        drawBackground(canvas, width, height);

        if (gba == null) {
            fallbackText.setColor(TEXT_LIGHT);
            fallbackText.setTextAlign(Paint.Align.CENTER);
            fallbackText.setTextSize(height / 20f);
            canvas.drawText("Font assets missing from the APK", width / 2f, height / 2f, fallbackText);
            return;
        }

        int margin = MARGIN * s;
        int gap = GAP * s;
        if (!state.inGame) {
            drawTitle(canvas, margin, width - margin, height);
            return;
        }

        int headerBottom = margin + lineBox(gba.main) + 2 * (PAD_Y + 1) * s;
        drawHeader(canvas, margin, margin, width - margin, headerBottom);
        int tabsTop = headerBottom + gap;
        int tabsBottom = tabsTop + lineBox(gba.main) + 2 * (PAD_Y + 1) * s;
        drawTabs(canvas, margin, tabsTop, width - margin, tabsBottom);
        int top = tabsBottom + gap;
        int bottom = height - margin;

        switch (tab) {
            case TAB_BATTLE:
                if (state.inBattle) {
                    // The party is a reference column; the battle gets the larger share.
                    int partyRight = margin + (width - 2 * margin - gap) * 34 / 100;
                    drawPartyColumn(canvas, margin, top, partyRight, bottom);
                    drawBattle(canvas, partyRight + gap, top, width - margin, bottom);
                } else {
                    drawMessage(canvas, margin, top, width - margin, bottom, "No battle right now.");
                }
                break;
            case TAB_BAG:
                drawBag(canvas, margin, top, width - margin, bottom);
                break;
            default:
                if (detailSlot >= 0 && detailSlot < state.party.size()) {
                    drawSummary(canvas, state.party.get(detailSlot), margin, top, width - margin, bottom);
                } else {
                    drawPartyGrid(canvas, margin, top, width - margin, bottom);
                }
                break;
        }

        // Item icons are decoded by the game a few frames after the first ask.
        if (waitingForIcons) {
            postInvalidateDelayed(150);
        }
    }

    /** Height of one line of a font family at the base scale. */
    private int lineBox(GbaFont[] family) {
        return family[0].boxHeight * s;
    }

    private void drawBackground(Canvas canvas, int width, int height) {
        canvas.drawColor(COLOR_BACKGROUND);
        // Faint stripes two pixels high, like the game's menu backdrops.
        int stripe = 2 * s;
        for (int y = stripe; y < height; y += 2 * stripe) {
            rect(canvas, 0, y, width, Math.min(height, y + stripe), COLOR_BACKGROUND_STRIPE);
        }
    }

    private void drawTitle(Canvas canvas, int left, int right, int height) {
        int boxHeight = 2 * lineBox(gba.main) + 4 * (PAD_Y + 1) * s;
        int top = (height - boxHeight) / 2;
        window(canvas, left, top, right, top + boxHeight, COLOR_DARK_WINDOW, COLOR_DARK_BORDER);
        int inner = right - left - 2 * (PAD_X + 1) * s;
        GbaText.Fit title = GbaText.fit(gba.main, "POKéMON HEART & SOUL", inner, boxHeight / 2, 2 * s, s, s);
        GbaText.Fit line = GbaText.fit(gba.small, "Your party shows here once a game is loaded.", inner,
                boxHeight / 2, s, s, Math.max(1, s - 1));
        int center = (left + right) / 2;
        GbaText.drawCentered(canvas, title, center - title.width / 2, top + boxHeight / 3, TEXT_LIGHT, SHADOW_LIGHT);
        GbaText.drawCentered(canvas, line, center - line.width / 2, top + boxHeight * 3 / 4, TEXT_LIGHT, SHADOW_LIGHT);
    }

    /** A dark window with one line of text in the middle of the page. */
    private void drawMessage(Canvas canvas, int left, int top, int right, int bottom, String text) {
        int boxHeight = lineBox(gba.main) + 4 * (PAD_Y + 1) * s;
        int boxTop = (top + bottom - boxHeight) / 2;
        window(canvas, left, boxTop, right, boxTop + boxHeight, COLOR_DARK_WINDOW, COLOR_DARK_BORDER);
        GbaText.Fit fit = GbaText.fit(gba.main, text, right - left - 2 * (PAD_X + 1) * s, boxHeight, s, s,
                Math.max(1, s - 1));
        GbaText.drawCentered(canvas, fit, (left + right - fit.width) / 2, boxTop + boxHeight / 2,
                TEXT_LIGHT, SHADOW_LIGHT);
    }

    /**
     * Player and map on the left; fast forward, money, badges and play time
     * on the right.
     */
    private void drawHeader(Canvas canvas, int left, int top, int right, int bottom) {
        window(canvas, left, top, right, bottom, COLOR_DARK_WINDOW, COLOR_DARK_BORDER);
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int inner = inRight - inLeft;
        int center = (top + bottom) / 2;
        int maxHeight = bottom - top - 2 * s;

        String info = String.format(Locale.US, "¥%d   BADGES %d   %d:%02d",
                state.money, state.badges, state.hours, state.minutes);
        GbaText.Fit infoFit = GbaText.fit(gba.main, info, inner * 50 / 100, maxHeight, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, infoFit, inRight - infoFit.width, center, TEXT_LIGHT, SHADOW_LIGHT);
        int infoLeft = inRight - infoFit.width;

        // Fast forward: the game's arrow twice, on the left of the numbers.
        if (state.fastForward) {
            String arrows = "▶▶";
            int pillWidth = gba.small[0].width(arrows) * s + 4 * s;
            int pillLeft = infoLeft - 2 * GAP * s - pillWidth;
            drawPill(canvas, arrows, COLOR_FAST_FORWARD, pillLeft, center, pillWidth);
            infoLeft = pillLeft;
        }

        int nameRoom = infoLeft - inLeft - 2 * GAP * s;
        GbaText.Fit player = GbaText.fit(gba.main, state.playerName, nameRoom, maxHeight, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, player, inLeft, center, TEXT_ACCENT, SHADOW_ACCENT);
        if (!state.mapName.isEmpty()) {
            int mapLeft = inLeft + player.width + (player.width > 0 ? GAP * s : 0);
            GbaText.Fit map = GbaText.fit(gba.main, state.mapName, inLeft + nameRoom - mapLeft, maxHeight,
                    s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, map, mapLeft, center, TEXT_LIGHT, SHADOW_LIGHT);
        }
    }

    /** PARTY / BATTLE / BAG. BATTLE can only be opened during a battle. */
    private void drawTabs(Canvas canvas, int left, int top, int right, int bottom) {
        int gap = GAP * s;
        int count = TAB_NAMES.length;
        int tabWidth = (right - left - (count - 1) * gap) / count;
        int labelScale = GbaText.groupScale(gba.main, TAB_NAMES, tabWidth - 2 * (PAD_X + 1) * s,
                bottom - top - 2 * s, s, s, Math.max(1, s - 1));
        for (int i = 0; i < count; i++) {
            int x = left + i * (tabWidth + gap);
            int x2 = i == count - 1 ? right : x + tabWidth;
            boolean enabled = i != TAB_BATTLE || state.inBattle;
            boolean selected = i == tab;
            Button button = addButton(x, top, x2, bottom, UI_TAB, i, enabled && !selected);
            boolean pressed = isPressed(button);
            if (selected) {
                window(canvas, x, top, x2, bottom, COLOR_ACTIVE_WINDOW, COLOR_ACTIVE_BORDER);
            } else {
                window(canvas, x, top, x2, bottom, pressed ? COLOR_WINDOW_BORDER : COLOR_DARK_WINDOW,
                        COLOR_DARK_BORDER);
            }
            String label = TAB_NAMES[i];
            if (i == TAB_BATTLE && state.inBattle && !selected) {
                label = "▶" + label; // a battle is on behind another tab
            }
            GbaText.Fit fit = GbaText.fit(gba.main, label, x2 - x - 2 * (PAD_X + 1) * s, bottom - top,
                    labelScale, labelScale, Math.max(1, labelScale - 1));
            int[] ink = selected ? new int[] { TEXT_DARK, SHADOW_DARK }
                    : enabled ? new int[] { TEXT_LIGHT, SHADOW_LIGHT } : new int[] { 0xFF68788C, COLOR_DARK_BORDER };
            GbaText.drawCentered(canvas, fit, (x + x2 - fit.width) / 2, (top + bottom) / 2, ink[0], ink[1]);
        }
    }

    // ------------------------------------------------------------------
    // Party
    // ------------------------------------------------------------------

    /** Six cards, two across, three down. Touch one for its summary. */
    private void drawPartyGrid(Canvas canvas, int left, int top, int right, int bottom) {
        int gap = GAP * s;
        int cardWidth = (right - left - gap) / 2;
        int cardHeight = (bottom - top - 2 * gap) / 3;
        for (int i = 0; i < 6; i++) {
            int x = left + (i % 2) * (cardWidth + gap);
            int y = top + (i / 2) * (cardHeight + gap);
            BottomScreenState.Mon mon = i < state.party.size() ? state.party.get(i) : null;
            boolean pressed = false;
            if (mon != null) {
                pressed = isPressed(addButton(x, y, x + cardWidth, y + cardHeight, UI_MON, i, true));
            }
            drawMonCard(canvas, mon, x, y, x + cardWidth, y + cardHeight, pressed, true);
        }
    }

    /** In battle: the party as a column beside the battle. */
    private void drawPartyColumn(Canvas canvas, int left, int top, int right, int bottom) {
        int gap = 2 * s;
        int cardHeight = (bottom - top - 5 * gap) / 6;
        for (int i = 0; i < 6; i++) {
            int y = top + i * (cardHeight + gap);
            BottomScreenState.Mon mon = i < state.party.size() ? state.party.get(i) : null;
            drawMonCard(canvas, mon, left, y, right, y + cardHeight, i == state.partyIndex, false);
        }
    }

    /**
     * A party member. On a card tall enough, its menu icon sits on the left.
     * The card shows as many of these rows as fit, in this order, and spreads
     * them over its height: name and level; status or "HP", HP bar and HP
     * numbers; held item; the four moves.
     */
    private void drawMonCard(Canvas canvas, BottomScreenState.Mon mon, int left, int top, int right, int bottom,
                             boolean active, boolean withMoves) {
        if (mon == null) {
            window(canvas, left, top, right, bottom, COLOR_EMPTY_WINDOW, COLOR_EMPTY_BORDER);
            return;
        }
        boolean fainted = !mon.egg && mon.hp == 0;
        window(canvas, left, top, right, bottom,
                active ? COLOR_ACTIVE_WINDOW : fainted ? COLOR_FAINTED_WINDOW : COLOR_WINDOW,
                active ? COLOR_ACTIVE_BORDER : COLOR_WINDOW_BORDER);

        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int inTop = top + (PAD_Y + 1) * s;
        int inBottom = bottom - (PAD_Y + 1) * s;
        int iconSize = 32 * s;
        if (withMoves && inBottom - inTop >= iconSize - 4 * s && inRight - inLeft >= 3 * iconSize) {
            // The icon's frame has empty rows; let it overlap the padding.
            drawMonIcon(canvas, mon, inLeft - 2 * s, (inTop + inBottom - iconSize) / 2, s, fainted);
            inLeft += iconSize - 2 * s;
        }
        int nameRow = lineBox(gba.main);
        int smallRow = pillHeight();
        int minGap = 2 * s;

        if (mon.egg) {
            GbaText.Fit egg = GbaText.fit(gba.main, "EGG", inRight - inLeft, inBottom - inTop, s, s, 1);
            GbaText.drawCentered(canvas, egg, inLeft, (inTop + inBottom) / 2, TEXT_DARK, SHADOW_DARK);
            return;
        }

        // Too short for the HP row (a battle column on a small screen): the
        // name and a bare HP bar under it.
        int compactBar = 6 * s;
        if (!withMoves && inBottom - inTop < nameRow + minGap + smallRow) {
            int content = nameRow + minGap + compactBar;
            int y = inTop + Math.max(0, (inBottom - inTop - content) / 2);
            drawNameRow(canvas, mon, inLeft, inRight, y + nameRow / 2, nameRow, fainted);
            drawBar(canvas, inLeft, y + nameRow + minGap + compactBar / 2, inRight, mon.hp, mon.maxHp, null);
            return;
        }

        // Rows that fit, in priority order: name, HP, held item (if any),
        // then the moves as two rows.
        final int rowName = 0;
        final int rowHp = 1;
        final int rowItem = 2;
        final int rowMoves = 3;
        int[] kinds = new int[4];
        int[] heights = new int[4];
        int rows = 0;
        int used = 0;
        int available = inBottom - inTop;
        int[] candidates = mon.item.isEmpty() ? new int[] { rowName, rowHp, rowMoves }
                : new int[] { rowName, rowHp, rowItem, rowMoves };
        for (int kind : candidates) {
            if (kind == rowMoves && !withMoves) {
                break;
            }
            // Moves come in pairs: one row of moves alone would hide two of them.
            int height = kind == rowName ? nameRow : kind == rowMoves ? 2 * smallRow + minGap : smallRow;
            int need = used + height + (rows > 0 ? minGap : 0);
            if (need > available && rows > 0) {
                break;
            }
            kinds[rows] = kind;
            heights[rows++] = height;
            used = need;
        }
        int contentHeight = 0;
        for (int i = 0; i < rows; i++) {
            contentHeight += heights[i];
        }
        int gap = rows > 1 ? Math.min((available - contentHeight) / (rows - 1), 6 * s) : 0;
        int y = inTop + (available - contentHeight - gap * (rows - 1)) / 2;

        for (int i = 0; i < rows; i++) {
            int center = y + heights[i] / 2;
            switch (kinds[i]) {
                case rowName:
                    drawNameRow(canvas, mon, inLeft, inRight, center, heights[i], fainted);
                    break;
                case rowHp:
                    drawHpRow(canvas, mon, inLeft, inRight, center, true);
                    break;
                case rowItem:
                    drawItemRow(canvas, mon, inLeft, inRight, center);
                    break;
                default:
                    drawMoveList(canvas, mon, inLeft, inRight, y, smallRow, smallRow + minGap);
                    break;
            }
            y += heights[i] + gap;
        }
    }

    /** Draws a mon's 32x32 menu icon at pixel scale scale. */
    private void drawMonIcon(Canvas canvas, BottomScreenState.Mon mon, int left, int top, int scale, boolean dim) {
        Bitmap icon = icons.mon(mon.iconSpecies, mon.personality, mon.egg);
        if (icon == null) {
            return;
        }
        drawBitmap(canvas, icon, left, top, scale, dim);
    }

    private void drawBitmap(Canvas canvas, Bitmap bitmap, int left, int top, int scale, boolean dim) {
        iconSource.set(0, 0, bitmap.getWidth(), bitmap.getHeight());
        iconTarget.set(left, top, left + bitmap.getWidth() * scale, top + bitmap.getHeight() * scale);
        pixels.setAlpha(dim ? 110 : 255);
        canvas.drawBitmap(bitmap, iconSource, iconTarget, pixels);
    }

    /** Name on the left, the Lv symbol and level on the right. */
    private void drawNameRow(Canvas canvas, BottomScreenState.Mon mon, int left, int right, int center, int height,
                             boolean dim) {
        GbaText.Fit level = GbaText.fit(gba.main, GbaFont.LV + String.valueOf(mon.level),
                (right - left) / 3, height, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, level, right - level.width, center, TEXT_DARK, SHADOW_DARK);
        GbaText.Fit name = GbaText.fit(gba.main, mon.displayName(), right - left - level.width - GAP * s,
                height, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, name, left, center, dim ? TEXT_DIM : TEXT_DARK, dim ? SHADOW_DIM : SHADOW_DARK);
    }

    /**
     * The status (or "HP" when healthy), the HP bar, and optionally the HP
     * numbers. The bar gets what the text leaves.
     */
    private void drawHpRow(Canvas canvas, BottomScreenState.Mon mon, int left, int right, int center,
                           boolean numbers) {
        int x = left;
        if (!mon.status.isEmpty()) {
            x = drawPill(canvas, mon.status, statusColor(mon.status), x, center, (right - left) / 3) + 2 * s;
        } else {
            GbaText.Fit hp = GbaText.fit(gba.small, "HP", (right - left) / 4, pillHeight(), s, s, 1);
            GbaText.drawCentered(canvas, hp, x, center, TEXT_DARK, SHADOW_DARK);
            x += hp.width + 2 * s;
        }
        int barRight = right;
        if (numbers) {
            String text = mon.hp + "/" + mon.maxHp;
            GbaText.Fit fit = GbaText.fit(gba.small, text, (right - x) * 45 / 100, pillHeight(),
                    s, s, Math.max(1, s - 1));
            // Only with a bar long enough to read beside it.
            if (right - x - fit.width - 3 * s >= 24 * s) {
                GbaText.drawCentered(canvas, fit, right - fit.width, center, TEXT_DARK, SHADOW_DARK);
                barRight = right - fit.width - 3 * s;
            }
        }
        drawBar(canvas, x, center, barRight, mon.hp, mon.maxHp, null);
    }

    /** A small bag symbol and the held item's name. */
    private void drawItemRow(Canvas canvas, BottomScreenState.Mon mon, int left, int right, int center) {
        if (mon.item.isEmpty()) {
            return;
        }
        int x = drawHeldItemMark(canvas, left, center);
        GbaText.Fit item = GbaText.fit(gba.small, mon.item, right - x, pillHeight(), s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, item, x, center, TEXT_DARK, SHADOW_DARK);
    }

    /** The held-item mark: a 6x6 bag with a darker band. Returns where text goes. */
    private int drawHeldItemMark(Canvas canvas, int left, int center) {
        int size = 6 * s;
        int top = center - size / 2;
        window(canvas, left, top, left + size, top + size, 0xFFF89078, 0xFF984838);
        rect(canvas, left + s, top + 2 * s, left + size - s, top + 3 * s, 0xFFC86050);
        return left + size + 2 * s;
    }

    /** The four moves, two per row, each with a mark in its type's color. */
    private void drawMoveList(Canvas canvas, BottomScreenState.Mon mon, int left, int right, int top, int rowHeight,
                              int rowPitch) {
        int columnGap = 4 * s;
        int columnWidth = (right - left - columnGap) / 2;
        int mark = 5 * s;
        int textWidth = columnWidth - mark - 2 * s;
        String[] names = new String[mon.moves.size()];
        for (int i = 0; i < names.length; i++) {
            names[i] = mon.moves.get(i).name;
        }
        int scale = GbaText.groupScale(gba.small, names, textWidth, rowHeight, s, s, Math.max(1, s - 1));
        for (int i = 0; i < mon.moves.size() && i < 4; i++) {
            BottomScreenState.Move move = mon.moves.get(i);
            int x = left + (i % 2) * (columnWidth + columnGap);
            int center = top + (i / 2) * rowPitch + rowHeight / 2;
            window(canvas, x, center - mark / 2, x + mark, center - mark / 2 + mark,
                    typeColor(move.type), darker(typeColor(move.type)));
            GbaText.Fit name = GbaText.fit(gba.small, move.name, textWidth, rowHeight, scale, scale, scale);
            GbaText.drawCentered(canvas, name, x + mark + 2 * s, center, TEXT_DARK, SHADOW_DARK);
        }
    }

    // ------------------------------------------------------------------
    // Summary
    // ------------------------------------------------------------------

    /**
     * One party member in full: a title bar with its icon, names, types and
     * the way back; then on the left the held item, ability, nature, EXP and
     * type matchups, on the right the stats with IVs and EVs; and the moves
     * along the bottom.
     */
    private void drawSummary(Canvas canvas, BottomScreenState.Mon mon, int left, int top, int right, int bottom) {
        int gap = GAP * s;
        int smallLine = lineBox(gba.small);
        int titleBottom = top + Math.max(32 * s, 2 * lineBox(gba.main)) + 2 * (PAD_Y + 1) * s - 4 * s;
        drawSummaryTitle(canvas, mon, left, top, right, titleBottom);

        if (mon.egg) {
            drawMessage(canvas, left, titleBottom + gap, right, bottom, "An egg. Who knows what will hatch?");
            return;
        }

        // Moves: two across, two lines each.
        int moveRow = pillHeight() + smallLine + 2 * s;
        int movesTop = bottom - (2 * moveRow + gap + 2 * (PAD_Y + 1) * s);
        drawSummaryMoves(canvas, mon, left, movesTop, right, bottom);

        int middleTop = titleBottom + gap;
        int middleBottom = movesTop - gap;
        int split = left + (right - left - gap) * 47 / 100;
        if (summaryMatchups) {
            drawSummaryMatchups(canvas, mon, left, middleTop, split, middleBottom);
        } else {
            drawSummaryInfo(canvas, mon, left, middleTop, split, middleBottom);
        }
        drawSummaryStats(canvas, mon, split + gap, middleTop, right, middleBottom);
    }

    private void drawSummaryTitle(Canvas canvas, BottomScreenState.Mon mon, int left, int top, int right,
                                  int bottom) {
        window(canvas, left, top, right, bottom, COLOR_DARK_WINDOW, COLOR_DARK_BORDER);
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int center = (top + bottom) / 2;
        int iconSize = 32 * s;
        drawMonIcon(canvas, mon, inLeft - 2 * s, center - iconSize / 2, s, false);
        int x = inLeft + iconSize;

        // BACK on the right, and arrows to step through the party.
        int buttonHeight = bottom - top - 4 * s;
        int backWidth = gba.main[0].width("BACK") * s + 2 * (PAD_X + 1) * s;
        int arrowWidth = gba.main[0].width(ARROW_RIGHT) * s + 2 * (PAD_X + 1) * s;
        int buttonTop = center - buttonHeight / 2;
        int backLeft = inRight - backWidth;
        drawSmallButton(canvas, "BACK", backLeft, buttonTop, inRight, buttonTop + buttonHeight, UI_BACK, 0, true);
        int nextLeft = backLeft - GAP * s - arrowWidth;
        int previousLeft = nextLeft - s - arrowWidth;
        boolean several = state.party.size() > 1;
        drawSmallButton(canvas, ARROW_RIGHT, nextLeft, buttonTop, nextLeft + arrowWidth, buttonTop + buttonHeight,
                UI_NEXT_MON, 0, several);
        drawSmallButton(canvas, ARROW_LEFT, previousLeft, buttonTop, previousLeft + arrowWidth, buttonTop + buttonHeight,
                UI_PREVIOUS_MON, 0, several);
        int textRight = previousLeft - GAP * s;

        // Top line: name, gender, Lv. Bottom line: species and types.
        int lineHeight = (bottom - top) / 2;
        int nameCenter = top + lineHeight / 2 + s;
        int typeCenter = bottom - lineHeight / 2 - s;
        // The gender mark in the game's blue or red, after the name.
        String gender = mon.gender == 0x00 ? "♂" : mon.gender == 0xFE ? "♀" : "";
        GbaText.Fit level = GbaText.fit(gba.main, GbaFont.LV + String.valueOf(mon.level), (textRight - x) / 3,
                lineHeight, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, level, textRight - level.width, nameCenter, TEXT_LIGHT, SHADOW_LIGHT);
        int genderWidth = gender.isEmpty() ? 0 : gba.main[0].width(gender) * s + 2 * s;
        GbaText.Fit nameFit = GbaText.fit(gba.main, mon.displayName(),
                textRight - x - level.width - GAP * s - genderWidth, lineHeight, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, nameFit, x, nameCenter, TEXT_ACCENT, SHADOW_ACCENT);
        if (!gender.isEmpty()) {
            GbaText.Fit genderFit = GbaText.fit(gba.main, gender, genderWidth, lineHeight, s, s, 1);
            boolean male = mon.gender == 0x00;
            GbaText.drawCentered(canvas, genderFit, x + nameFit.width + 2 * s, nameCenter,
                    male ? 0xFF70A8F8 : 0xFFF87878, male ? 0xFF203868 : 0xFF682020);
        }

        int typeX = x;
        if (!mon.nick.isEmpty() && !mon.nick.equals(mon.species)) {
            GbaText.Fit species = GbaText.fit(gba.small, "/" + mon.species, (textRight - x) / 2, lineHeight,
                    s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, species, typeX, typeCenter, TEXT_LIGHT, SHADOW_LIGHT);
            typeX += species.width + GAP * s;
        }
        int typesRight = drawTypePills(canvas, mon.types, typeX, typeCenter, textRight);
        if (mon.shiny && textRight - typesRight > 12 * s) {
            drawPill(canvas, "SHINY", COLOR_CURSOR, typesRight, typeCenter, textRight - typesRight);
        }
    }

    /** One pill per type, left to right. Returns the right edge. */
    private int drawTypePills(Canvas canvas, int[] types, int left, int center, int right) {
        TypeChart chart = typeChart();
        int x = left;
        for (int type : types) {
            String name = chart != null ? chart.name(type) : "";
            if (name.isEmpty() || right - x < 12 * s) {
                continue;
            }
            x = drawPill(canvas, name, typeColor(name), x, center, right - x) + 2 * s;
        }
        return x;
    }

    /** Held item, ability, nature, EXP to the next level, then matchups. */
    private void drawSummaryInfo(Canvas canvas, BottomScreenState.Mon mon, int left, int top, int right,
                                 int bottom) {
        window(canvas, left, top, right, bottom, COLOR_WINDOW, COLOR_WINDOW_BORDER);
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int row = pillHeight() + s;
        int y = top + (PAD_Y + 1) * s;
        int labelWidth = (inRight - inLeft) * 40 / 100;

        String[][] lines = {
            { "ITEM", mon.item.isEmpty() ? "NONE" : mon.item },
            { "ABILITY", mon.ability },
            { "NATURE", mon.nature },
        };
        for (String[] line : lines) {
            int center = y + row / 2;
            GbaText.Fit label = GbaText.fit(gba.small, line[0], labelWidth, row, s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, label, inLeft, center, TEXT_DIM, SHADOW_DIM);
            GbaText.Fit value = GbaText.fit(gba.small, line[1], inRight - inLeft - labelWidth, row, s, s,
                    Math.max(1, s - 1));
            GbaText.drawCentered(canvas, value, inLeft + labelWidth, center, TEXT_DARK, SHADOW_DARK);
            y += row;
        }

        // EXP: a bar like the summary screen's, and the points still to go.
        int center = y + row / 2;
        GbaText.Fit expLabel = GbaText.fit(gba.small, "EXP", labelWidth, row, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, expLabel, inLeft, center, TEXT_DIM, SHADOW_DIM);
        if (mon.expNext > 0) {
            String toGo = (mon.expNext - mon.exp) + " TO " + GbaFont.LV + (mon.level + 1);
            GbaText.Fit toGoFit = GbaText.fit(gba.small, toGo, (inRight - inLeft - labelWidth) * 60 / 100, row,
                    s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, toGoFit, inRight - toGoFit.width, center, TEXT_DARK, SHADOW_DARK);
            drawBar(canvas, inLeft + labelWidth, center, inRight - toGoFit.width - 3 * s, mon.exp, mon.expNext,
                    EXP_FILL);
        } else {
            GbaText.Fit max = GbaText.fit(gba.small, "MAX LEVEL", inRight - inLeft - labelWidth, row, s, s,
                    Math.max(1, s - 1));
            GbaText.drawCentered(canvas, max, inLeft + labelWidth, center, TEXT_DARK, SHADOW_DARK);
        }
        y += row + 2 * s;

        // Matchups from the type chart, by the mon's types alone. When they
        // do not all fit under the info, a button opens them on their own.
        TypeChart chart = typeChart();
        if (chart == null || y + row > bottom) {
            return;
        }
        rect(canvas, inLeft, y, inRight, y + s, SHADOW_DARK);
        y += 2 * s;
        int limit = bottom - (PAD_Y + 1) * s;
        if (drawMatchups(canvas, chart, mon, inLeft, inRight, y, limit, labelWidth, false)) {
            drawMatchups(canvas, chart, mon, inLeft, inRight, y, limit, labelWidth, true);
        } else {
            int buttonBottom = Math.min(limit, y + row + 4 * s);
            drawSmallButton(canvas, "TYPE MATCHUPS " + ARROW_RIGHT, inLeft, y, inRight, buttonBottom,
                    UI_MATCHUPS, 1, true);
        }
    }

    /** The matchups alone, filling the info window, with the way back. */
    private void drawSummaryMatchups(Canvas canvas, BottomScreenState.Mon mon, int left, int top, int right,
                                     int bottom) {
        window(canvas, left, top, right, bottom, COLOR_WINDOW, COLOR_WINDOW_BORDER);
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int row = pillHeight() + s;
        int y = top + (PAD_Y + 1) * s;
        GbaText.Fit title = GbaText.fit(gba.small, "TYPE MATCHUPS", (inRight - inLeft) / 2, row, s, s,
                Math.max(1, s - 1));
        GbaText.drawCentered(canvas, title, inLeft, y + row / 2, TEXT_DIM, SHADOW_DIM);
        int backWidth = gba.small[0].width("INFO") * s + 8 * s;
        drawSmallButton(canvas, "INFO", inRight - backWidth, y, inRight, y + row, UI_MATCHUPS, 0, true);
        y += row + 2 * s;
        TypeChart chart = typeChart();
        if (chart != null) {
            drawMatchups(canvas, chart, mon, inLeft, inRight, y, bottom - (PAD_Y + 1) * s, -1, true);
        }
    }

    /**
     * WEAK, RESISTS and IMMUNE, each with its types as pills, wrapping onto
     * more lines. The pills start in a column labelWidth from the left, or
     * with labelWidth -1 straight after the label, and wrapped lines then
     * use the full width. With draw false nothing is drawn. Returns whether
     * all of it fits above limit.
     */
    private boolean drawMatchups(Canvas canvas, TypeChart chart, BottomScreenState.Mon mon, int inLeft,
                                 int inRight, int y, int limit, int labelWidth, boolean draw) {
        int row = pillHeight() + s;
        int[][] matchups = chart.matchups(mon.types);
        String[] groups = { "WEAK", "RESISTS", "IMMUNE" };
        for (int g = 0; g < groups.length; g++) {
            List<int[]> group = new ArrayList<>();
            for (int[] matchup : matchups) {
                int multiplier = matchup[1];
                boolean in = g == 0 ? multiplier > TypeChart.NEUTRAL
                        : g == 1 ? multiplier > 0 && multiplier < TypeChart.NEUTRAL : multiplier == 0;
                if (in) {
                    group.add(matchup);
                }
            }
            if (group.isEmpty()) {
                continue;
            }
            if (y + row > limit) {
                return false;
            }
            int lineCenter = y + row / 2;
            boolean inline = labelWidth < 0;
            GbaText.Fit label = GbaText.fit(gba.small, groups[g], inline ? inRight - inLeft : labelWidth, row,
                    s, s, Math.max(1, s - 1));
            if (draw) {
                GbaText.drawCentered(canvas, label, inLeft, lineCenter, TEXT_DIM, SHADOW_DIM);
            }
            int column = inline ? inLeft : inLeft + labelWidth;
            int x = inline ? inLeft + label.width + 3 * s : column;
            for (int[] matchup : group) {
                String name = chart.name(matchup[0]);
                // Four times and a quarter get a second mark.
                boolean double4 = matchup[1] >= 4 * TypeChart.NEUTRAL || (matchup[1] > 0
                        && matchup[1] <= TypeChart.NEUTRAL / 4);
                String text = abbreviate(name) + (double4 ? (matchup[1] > TypeChart.NEUTRAL ? "×4" : "×.25") : "");
                int pillWidth = gba.small[0].width(text) * s + 4 * s;
                if (x + pillWidth > inRight) {
                    y += row;
                    lineCenter = y + row / 2;
                    x = column;
                    if (y + row > limit) {
                        return false;
                    }
                }
                if (draw) {
                    x = drawPill(canvas, text, typeColor(name), x, lineCenter, inRight - x) + s;
                } else {
                    x += pillWidth + s;
                }
            }
            y += row;
        }
        return true;
    }

    /** Type names short enough to sit several to a line. */
    private static String abbreviate(String type) {
        switch (type.toUpperCase(Locale.US)) {
            case "FIGHTING": return "FIGHT";
            case "ELECTRIC": return "ELEC";
            case "PSYCHIC":  return "PSYCH";
            default:         return type.length() > 6 ? type.substring(0, 6) : type;
        }
    }

    /** Six rows: the stat, its value, IV and EV. The nature's colors as in the game. */
    private void drawSummaryStats(Canvas canvas, BottomScreenState.Mon mon, int left, int top, int right,
                                  int bottom) {
        window(canvas, left, top, right, bottom, COLOR_WINDOW, COLOR_WINDOW_BORDER);
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int inTop = top + (PAD_Y + 1) * s;
        int inBottom = bottom - (PAD_Y + 1) * s;
        int rows = 1 + BottomScreenState.STAT_COUNT + 1; // heading, six stats, totals
        int row = (inBottom - inTop) / rows;
        int width = inRight - inLeft;
        // Columns: name, value, IV, EV.
        int[] columnRight = { inLeft + width * 46 / 100, inLeft + width * 66 / 100, inLeft + width * 82 / 100,
                inRight };
        String[] headings = { "", "STAT", "IV", "EV" };
        for (int c = 1; c < 4; c++) {
            GbaText.Fit fit = GbaText.fit(gba.small, headings[c], columnRight[c] - columnRight[c - 1], row,
                    s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, fit, columnRight[c] - fit.width, inTop + row / 2, TEXT_DIM, SHADOW_DIM);
        }
        int ivTotal = 0;
        int evTotal = 0;
        for (int i = 0; i < BottomScreenState.STAT_COUNT; i++) {
            int center = inTop + (i + 1) * row + row / 2;
            if (i % 2 == 0) {
                rect(canvas, inLeft - s, center - row / 2, inRight + s, center - row / 2 + row, 0xFFF0F0E4);
            }
            int text = i == mon.natureUp ? TEXT_RAISED : i == mon.natureDown ? TEXT_LOWERED : TEXT_DARK;
            int shadow = i == mon.natureUp ? SHADOW_RAISED : i == mon.natureDown ? SHADOW_LOWERED : SHADOW_DARK;
            GbaText.Fit name = GbaText.fit(gba.small, STAT_NAMES[i], columnRight[0] - inLeft, row, s, s,
                    Math.max(1, s - 1));
            GbaText.drawCentered(canvas, name, inLeft, center, text, shadow);
            int[] values = { mon.stats[i], mon.ivs[i], mon.evs[i] };
            for (int c = 1; c < 4; c++) {
                String value = i == 0 && c == 1 ? mon.hp + "/" + values[0] : String.valueOf(values[c - 1]);
                GbaText.Fit fit = GbaText.fit(gba.small, value, columnRight[c] - columnRight[c - 1] - s, row,
                        s, s, Math.max(1, s - 1));
                boolean perfect = c == 2 && values[1] == 31;
                GbaText.drawCentered(canvas, fit, columnRight[c] - fit.width, center,
                        perfect ? 0xFF289848 : TEXT_DARK, perfect ? 0xFFB8E8C0 : SHADOW_DARK);
            }
            ivTotal += mon.ivs[i];
            evTotal += mon.evs[i];
        }
        int center = inTop + (BottomScreenState.STAT_COUNT + 1) * row + row / 2;
        GbaText.Fit total = GbaText.fit(gba.small, "TOTAL", columnRight[1] - inLeft, row, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, total, inLeft, center, TEXT_DIM, SHADOW_DIM);
        int[] totals = { ivTotal, evTotal };
        for (int c = 2; c < 4; c++) {
            GbaText.Fit fit = GbaText.fit(gba.small, String.valueOf(totals[c - 2]),
                    columnRight[c] - columnRight[c - 1] - s, row, s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, fit, columnRight[c] - fit.width, center, TEXT_DIM, SHADOW_DIM);
        }
    }

    /** The moves two across: type and name, then category, power, accuracy and PP. */
    private void drawSummaryMoves(Canvas canvas, BottomScreenState.Mon mon, int left, int top, int right,
                                  int bottom) {
        window(canvas, left, top, right, bottom, COLOR_WINDOW, COLOR_WINDOW_BORDER);
        int gap = GAP * s;
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int inTop = top + (PAD_Y + 1) * s;
        int inBottom = bottom - (PAD_Y + 1) * s;
        int columnWidth = (inRight - inLeft - 2 * gap) / 2;
        int rowHeight = (inBottom - inTop - gap) / 2;
        for (int i = 0; i < 4; i++) {
            int x = inLeft + (i % 2) * (columnWidth + 2 * gap);
            int y = inTop + (i / 2) * (rowHeight + gap);
            if (i % 2 == 1) {
                rect(canvas, x - gap - s / 2, y, x - gap + s - s / 2, y + rowHeight, SHADOW_DARK);
            }
            if (i >= mon.moves.size()) {
                GbaText.Fit dash = GbaText.fit(gba.main, "-", columnWidth, rowHeight, s, s, 1);
                GbaText.drawCentered(canvas, dash, x, y + rowHeight / 2, TEXT_DIM, SHADOW_DIM);
                continue;
            }
            BottomScreenState.Move move = mon.moves.get(i);
            int line1 = y + pillHeight() / 2;
            int line2 = y + rowHeight - lineBox(gba.small) / 2;
            int nameLeft = move.type.isEmpty() ? x
                    : drawPill(canvas, move.type, typeColor(move.type), x, line1, columnWidth / 2) + 2 * s;
            GbaText.Fit name = GbaText.fit(gba.small, move.name, x + columnWidth - nameLeft, pillHeight(),
                    s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, name, nameLeft, line1, TEXT_DARK, SHADOW_DARK);

            String category = move.category == BottomScreenState.CATEGORY_PHYSICAL ? "PHYS"
                    : move.category == BottomScreenState.CATEGORY_SPECIAL ? "SPEC" : "STATUS";
            String power = move.power == 0 ? "-" : move.power == 1 ? "?" : String.valueOf(move.power);
            String accuracy = move.accuracy == 0 ? "-" : move.accuracy + "%";
            String details = category + "  PWR " + power + "  ACC " + accuracy;
            String pp = "PP " + move.pp + "/" + move.maxPp;
            int[] ppInk = ppColor(move.pp, move.maxPp);
            GbaText.Fit ppFit = GbaText.fit(gba.small, pp, columnWidth / 3, lineBox(gba.small) + 2 * s, s, s,
                    Math.max(1, s - 1));
            GbaText.drawCentered(canvas, ppFit, x + columnWidth - ppFit.width, line2, ppInk[0], ppInk[1]);
            GbaText.Fit detailsFit = GbaText.fit(gba.small, details, columnWidth - ppFit.width - 2 * s,
                    lineBox(gba.small) + 2 * s, s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, detailsFit, x, line2, TEXT_DIM, SHADOW_DIM);
        }
    }

    // ------------------------------------------------------------------
    // Battle
    // ------------------------------------------------------------------

    private void drawBattle(Canvas canvas, int left, int top, int right, int bottom) {
        int gap = GAP * s;
        boolean canChoose = state.menu == BottomScreenState.MENU_ACTION || state.menu == BottomScreenState.MENU_MOVE;

        // The foe: a name line, a types line and an HP bar line.
        int foeBottom = top + lineBox(gba.main) + 2 * pillHeight() + (2 * (PAD_Y + 1) + 5) * s;
        drawFoe(canvas, left, top, right, foeBottom);

        // Bag / Pokémon / Run take a sixth of the height, or one roomy line.
        int actionHeight = Math.max(lineBox(gba.main) + 8 * s, (bottom - top) * 17 / 100);
        int actionTop = bottom - actionHeight;
        int movesTop = foeBottom + gap;
        int movesBottom = actionTop - gap;

        // Moves, in the game's own cursor order: 0 1 / 2 3.
        int moveWidth = (right - left - gap) / 2;
        int moveHeight = (movesBottom - movesTop - gap) / 2;
        String[] names = new String[4];
        for (int i = 0; i < 4; i++) {
            BottomScreenState.Move move = moveAt(i);
            names[i] = move != null ? move.name : null;
        }
        int nameWidth = moveWidth - 2 * (PAD_X + 1) * s;
        int nameHeight = moveHeight - 2 * pillHeight() - 2 * (PAD_Y + 1) * s - 3 * s;
        int nameScale = GbaText.groupScale(gba.main, names, nameWidth, nameHeight, s + 1, s, Math.max(1, s - 1));
        for (int i = 0; i < 4; i++) {
            int x = left + (i % 2) * (moveWidth + gap);
            int y = movesTop + (i / 2) * (moveHeight + gap);
            drawMoveButton(canvas, moveAt(i), i, x, y, x + moveWidth, y + moveHeight, nameScale, canChoose);
        }

        if (state.menu == BottomScreenState.MENU_TARGET) {
            window(canvas, left, actionTop, right, bottom, COLOR_DARK_WINDOW, COLOR_DARK_BORDER);
            GbaText.Fit message = GbaText.fit(gba.main, "Choose the target with the buttons.",
                    right - left - 2 * (PAD_X + 1) * s, actionHeight, s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, message, (left + right - message.width) / 2, (actionTop + bottom) / 2,
                    TEXT_LIGHT, SHADOW_LIGHT);
            return;
        }

        // Bag, Pokémon, Run.
        String[] labels = { "BAG", "POKéMON", "RUN" };
        int[] actions = { DualScreenBridge.ACTION_BAG, DualScreenBridge.ACTION_POKEMON, DualScreenBridge.ACTION_RUN };
        int[] colors = { COLOR_BAG, COLOR_POKEMON, COLOR_RUN };
        int actionWidth = (right - left - 2 * gap) / 3;
        int labelWidth = actionWidth - 2 * (PAD_X + 1) * s;
        int labelScale = GbaText.groupScale(gba.main, labels, labelWidth, actionHeight - 4 * s, s + 2, s,
                Math.max(1, s - 1));
        for (int i = 0; i < 3; i++) {
            int x = left + i * (actionWidth + gap);
            int x2 = i == 2 ? right : x + actionWidth;
            Button button = addButton(x, actionTop, x2, bottom, DualScreenBridge.TAP_ACTION, actions[i], canChoose);
            boolean pressed = isPressed(button);
            boolean cursor = state.menu == BottomScreenState.MENU_ACTION && state.actionCursor == actions[i];
            drawButtonFrame(canvas, x, actionTop, x2, bottom, colors[i], pressed, cursor);
            GbaText.Fit label = GbaText.fit(gba.main, labels[i], labelWidth, actionHeight, labelScale, labelScale,
                    labelScale);
            int[] ink = pressed ? onColor(colors[i]) : new int[] { TEXT_DARK, SHADOW_DARK };
            GbaText.drawCentered(canvas, label, (x + x2 - label.width) / 2, (actionTop + bottom) / 2, ink[0], ink[1]);
            if (!canChoose) {
                dimOver(canvas, x, actionTop, x2, bottom);
            }
        }
    }

    private BottomScreenState.Move moveAt(int slot) {
        if (state.battleMon != null && slot < state.battleMon.moves.size()) {
            return state.battleMon.moves.get(slot);
        }
        return null;
    }

    private void drawFoe(Canvas canvas, int left, int top, int right, int bottom) {
        window(canvas, left, top, right, bottom, COLOR_WINDOW, COLOR_WINDOW_BORDER);
        BottomScreenState.Mon foe = state.foe;
        if (foe == null) {
            return;
        }
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int inTop = top + (PAD_Y + 1) * s;
        int nameCenter = inTop + lineBox(gba.main) / 2;
        int typeCenter = nameCenter + lineBox(gba.main) / 2 + 2 * s + pillHeight() / 2;
        int barCenter = bottom - (PAD_Y + 1) * s - pillHeight() / 2;

        // "WILD" or "FOE" as a tag, then the name and level as in a card.
        int x = drawPill(canvas, state.trainerBattle ? "FOE" : "WILD", COLOR_WINDOW_BORDER, inLeft, nameCenter,
                (inRight - inLeft) / 4) + 3 * s;
        drawNameRow(canvas, foe, x, inRight, nameCenter, lineBox(gba.main), false);

        // Its types, and against a trainer one ball per Pokémon the trainer
        // brought, filled while it can still fight.
        int typesRight = inRight;
        if (state.trainerBattle && state.foeTeam > 0) {
            int ball = 6 * s;
            int pitch = ball + 2 * s;
            int ballsLeft = inRight - state.foeTeam * pitch + 2 * s;
            for (int i = 0; i < state.foeTeam; i++) {
                int bx = ballsLeft + i * pitch;
                boolean left6 = i < state.foeTeamLeft;
                window(canvas, bx, typeCenter - ball / 2, bx + ball, typeCenter - ball / 2 + ball,
                        left6 ? 0xFFE84830 : 0xFFB0B0A8, left6 ? 0xFF802018 : 0xFF707068);
                rect(canvas, bx + s, typeCenter - s / 2, bx + ball - s, typeCenter - s / 2 + s,
                        left6 ? 0xFFF8F8F8 : 0xFFD8D8D0);
            }
            typesRight = ballsLeft - GAP * s;
        }
        drawTypePills(canvas, foe.types, inLeft, typeCenter, typesRight);

        // The game shows no HP numbers for the foe, only the bar.
        drawHpRow(canvas, foe, inLeft, inRight, barCenter, false);
    }

    /** SUPER, RESISTED or NO EFFECT for a damaging move against the foe; null otherwise. */
    private String effectiveness(BottomScreenState.Move move) {
        TypeChart chart = typeChart();
        if (chart == null || state.foe == null || move.category == BottomScreenState.CATEGORY_STATUS
                || state.foe.types.length == 0) {
            return null;
        }
        int multiplier = chart.multiplier(move.typeId, state.foe.types);
        if (multiplier == 0) {
            return "NO EFFECT";
        }
        if (multiplier >= 4 * TypeChart.NEUTRAL) {
            return "SUPER ×4";
        }
        if (multiplier > TypeChart.NEUTRAL) {
            return "SUPER";
        }
        if (multiplier < TypeChart.NEUTRAL) {
            return "RESISTED";
        }
        return null;
    }

    private void drawMoveButton(Canvas canvas, BottomScreenState.Move move, int slot, int left, int top, int right,
                                int bottom, int nameScale, boolean canChoose) {
        if (move == null) {
            window(canvas, left, top, right, bottom, COLOR_EMPTY_WINDOW, COLOR_EMPTY_BORDER);
            GbaText.Fit dash = GbaText.fit(gba.main, "-", right - left, bottom - top, s, s, 1);
            GbaText.drawCentered(canvas, dash, (left + right - dash.width) / 2, (top + bottom) / 2,
                    TEXT_LIGHT, SHADOW_LIGHT);
            return;
        }

        Button button = addButton(left, top, right, bottom, DualScreenBridge.TAP_MOVE, slot, canChoose);
        boolean pressed = isPressed(button);
        boolean cursor = state.menu == BottomScreenState.MENU_MOVE && slot == state.moveCursor;
        int type = typeColor(move.type);
        drawButtonFrame(canvas, left, top, right, bottom, type, pressed, cursor);

        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int infoCenter = bottom - (PAD_Y + 1) * s - s - pillHeight() / 2;
        int infoTop = infoCenter - pillHeight() / 2;
        int[] nameInk = pressed ? onColor(type) : new int[] { move.pp > 0 ? TEXT_DARK : TEXT_DIM,
                move.pp > 0 ? SHADOW_DARK : SHADOW_DIM };

        // Bottom line: PP on the right first, then the type tag gets the rest,
        // so the two can never overlap.
        String pp = "PP " + move.pp + "/" + move.maxPp;
        GbaText.Fit ppFit = GbaText.fit(gba.main, pp, (inRight - inLeft) / 2, pillHeight() + 2 * s,
                s, s, Math.max(1, s - 1));
        int[] ppInk = pressed ? onColor(type) : ppColor(move.pp, move.maxPp);
        GbaText.drawCentered(canvas, ppFit, inRight - ppFit.width, infoCenter, ppInk[0], ppInk[1]);
        if (!move.type.isEmpty()) {
            drawPill(canvas, move.type, type, inLeft, infoCenter, inRight - inLeft - ppFit.width - 3 * s);
        }

        // Top line: how it does against the foe's types.
        int nameTop = top + (PAD_Y + 1) * s;
        String effect = effectiveness(move);
        if (effect != null) {
            int color = effect.startsWith("SUPER") ? COLOR_SUPER : effect.equals("RESISTED") ? COLOR_RESISTED
                    : COLOR_IMMUNE;
            int tagCenter = nameTop + pillHeight() / 2;
            GbaText.Fit tag = GbaText.fit(gba.small, effect, inRight - inLeft - 4 * s, pillHeight(), s, s,
                    Math.max(1, s - 1));
            int tagWidth = tag.width + 4 * s;
            drawPill(canvas, effect, color, inRight - tagWidth, tagCenter, tagWidth);
            nameTop += pillHeight();
        }

        // The name, centred in the space between.
        GbaText.Fit name = GbaText.fit(gba.main, move.name, inRight - inLeft, infoTop - nameTop - s,
                nameScale, nameScale, Math.max(1, Math.min(nameScale, s - 1)));
        GbaText.drawCentered(canvas, name, (left + right - name.width) / 2, (nameTop + infoTop) / 2,
                nameInk[0], nameInk[1]);

        if (!canChoose) {
            dimOver(canvas, left, top, right, bottom);
        }
    }

    /** A button in a light tint of its color, with a darker border. */
    private void drawButtonFrame(Canvas canvas, int left, int top, int right, int bottom, int color,
                                 boolean pressed, boolean cursor) {
        if (cursor) {
            // The game's own cursor, so both screens agree.
            window(canvas, left - 2 * s, top - 2 * s, right + 2 * s, bottom + 2 * s, COLOR_CURSOR, COLOR_CURSOR);
        }
        window(canvas, left, top, right, bottom, pressed ? color : mix(color, 0xFFFFFFFF, 70), darker(color));
        // A band of the full color along the bottom edge.
        if (!pressed) {
            rect(canvas, left + s, bottom - 3 * s, right - s, bottom - s, color);
        }
    }

    // ------------------------------------------------------------------
    // Bag
    // ------------------------------------------------------------------

    private void drawBag(Canvas canvas, int left, int top, int right, int bottom) {
        if (bag == null) {
            drawMessage(canvas, left, top, right, bottom, "Opening the bag…");
            return;
        }
        if (bag.pockets.isEmpty()) {
            drawMessage(canvas, left, top, right, bottom, "The bag cannot be read right now.");
            return;
        }
        int gap = GAP * s;
        pocket = Math.max(0, Math.min(pocket, bag.pockets.size() - 1));

        // Pockets along the top.
        int pocketsBottom = top + lineBox(gba.small) + 2 * (PAD_Y + 1) * s;
        int count = bag.pockets.size();
        int pocketWidth = (right - left - (count - 1) * s) / count;
        String[] labels = new String[count];
        for (int i = 0; i < count; i++) {
            BagState.Pocket p = bag.pockets.get(i);
            labels[i] = p.id < POCKET_SHORT_NAMES.length ? POCKET_SHORT_NAMES[p.id] : p.name;
        }
        int labelScale = GbaText.groupScale(gba.small, labels, pocketWidth - 2 * PAD_X * s,
                pocketsBottom - top - 2 * s, s, s, Math.max(1, s - 1));
        for (int i = 0; i < count; i++) {
            int x = left + i * (pocketWidth + s);
            int x2 = i == count - 1 ? right : x + pocketWidth;
            boolean selected = i == pocket;
            Button button = addButton(x, top, x2, pocketsBottom, UI_POCKET, i, !selected);
            boolean pressed = isPressed(button);
            window(canvas, x, top, x2, pocketsBottom,
                    selected ? COLOR_ACTIVE_WINDOW : pressed ? COLOR_WINDOW_BORDER : COLOR_DARK_WINDOW,
                    selected ? COLOR_ACTIVE_BORDER : COLOR_DARK_BORDER);
            GbaText.Fit fit = GbaText.fit(gba.small, labels[i], x2 - x - 2 * PAD_X * s, pocketsBottom - top,
                    labelScale, labelScale, Math.max(1, labelScale - 1));
            GbaText.drawCentered(canvas, fit, (x + x2 - fit.width) / 2, (top + pocketsBottom) / 2,
                    selected ? TEXT_DARK : TEXT_LIGHT, selected ? SHADOW_DARK : SHADOW_LIGHT);
        }

        List<BagState.Item> items = bag.pockets.get(pocket).items;
        int listTop = pocketsBottom + gap;
        int split = left + (right - left - gap) * 56 / 100;
        bagSelected = Math.max(0, Math.min(bagSelected, items.size() - 1));
        drawBagList(canvas, items, left, listTop, split, bottom);
        drawBagItem(canvas, items.isEmpty() ? null : items.get(bagSelected), split + gap, listTop, right, bottom);
    }

    private void drawBagList(Canvas canvas, List<BagState.Item> items, int left, int top, int right, int bottom) {
        window(canvas, left, top, right, bottom, COLOR_WINDOW, COLOR_WINDOW_BORDER);
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int inTop = top + (PAD_Y + 1) * s;
        int inBottom = bottom - (PAD_Y + 1) * s;
        if (items.isEmpty()) {
            GbaText.Fit empty = GbaText.fit(gba.main, "Nothing here.", inRight - inLeft, inBottom - inTop, s, s,
                    Math.max(1, s - 1));
            GbaText.drawCentered(canvas, empty, inLeft, inTop + lineBox(gba.main) / 2, TEXT_DIM, SHADOW_DIM);
            return;
        }

        bagRowHeight = lineBox(gba.main) + 2 * s;
        bagRows = Math.max(1, (inBottom - inTop) / bagRowHeight);
        bagListRect.set(left, top, right, bottom);
        boolean scrolls = items.size() > bagRows;
        int maxScroll = Math.max(0, items.size() - bagRows);
        bagScroll = Math.max(0, Math.min(bagScroll, maxScroll));

        // Up and down on the right when the list is longer than the window.
        int arrowWidth = scrolls ? gba.main[0].width(ARROW_UP) * s + 4 * s : 0;
        int textRight = inRight - (scrolls ? arrowWidth + 2 * s : 0);
        if (scrolls) {
            int arrowHeight = bagRowHeight * 2;
            drawSmallButton(canvas, ARROW_UP, inRight - arrowWidth, inTop, inRight, inTop + arrowHeight, UI_SCROLL, 0,
                    bagScroll > 0);
            drawSmallButton(canvas, ARROW_DOWN, inRight - arrowWidth, inBottom - arrowHeight, inRight, inBottom, UI_SCROLL,
                    1, bagScroll < maxScroll);
            // Where the window is in the list.
            int trackTop = inTop + arrowHeight + 2 * s;
            int trackBottom = inBottom - arrowHeight - 2 * s;
            if (trackBottom - trackTop > 4 * s) {
                int thumb = Math.max(4 * s, (trackBottom - trackTop) * bagRows / items.size());
                int thumbTop = trackTop + (trackBottom - trackTop - thumb) * bagScroll / Math.max(1, maxScroll);
                int cx = inRight - arrowWidth / 2;
                rect(canvas, cx - s, trackTop, cx + s, trackBottom, SHADOW_DARK);
                rect(canvas, cx - s, thumbTop, cx + s, thumbTop + thumb, COLOR_WINDOW_BORDER);
            }
        }

        for (int row = 0; row < bagRows && bagScroll + row < items.size(); row++) {
            int index = bagScroll + row;
            BagState.Item item = items.get(index);
            int y = inTop + row * bagRowHeight;
            int center = y + bagRowHeight / 2;
            Button button = addButton(left, y, textRight, y + bagRowHeight, UI_ITEM, index, true);
            if (index == bagSelected || isPressed(button)) {
                rect(canvas, inLeft - 2 * s, y, textRight + s, y + bagRowHeight, COLOR_ROW_SELECTED);
            }
            if (index == bagSelected) {
                GbaText.Fit cursor = GbaText.fit(gba.main, "▶", 8 * s, bagRowHeight, s, s, 1);
                GbaText.drawCentered(canvas, cursor, inLeft - s, center, COLOR_ACTIVE_BORDER, SHADOW_DARK);
            }
            int nameLeft = inLeft + 7 * s;
            // Key items come one of each.
            String quantity = bag.pockets.get(pocket).id == 5 ? "" : "×" + item.quantity;
            GbaText.Fit qty = GbaText.fit(gba.main, quantity, (textRight - nameLeft) / 3, bagRowHeight, s, s,
                    Math.max(1, s - 1));
            GbaText.drawCentered(canvas, qty, textRight - qty.width, center, TEXT_DARK, SHADOW_DARK);
            String label = item.move.isEmpty() ? item.name : item.name + " " + item.move;
            GbaText.Fit name = GbaText.fit(gba.main, label, textRight - nameLeft - qty.width - GAP * s,
                    bagRowHeight, s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, name, nameLeft, center, TEXT_DARK, SHADOW_DARK);
        }
    }

    /** The touched item: its icon twice the size, name, move for a TM, and description. */
    private void drawBagItem(Canvas canvas, BagState.Item item, int left, int top, int right, int bottom) {
        window(canvas, left, top, right, bottom, COLOR_WINDOW, COLOR_WINDOW_BORDER);
        if (item == null) {
            return;
        }
        int inLeft = left + (PAD_X + 1) * s;
        int inRight = right - (PAD_X + 1) * s;
        int y = top + (PAD_Y + 1) * s + s;

        Bitmap icon = icons.item(item.id);
        int iconScale = 2 * s;
        int iconSize = 24 * iconScale;
        if (inRight - inLeft < iconSize) {
            iconScale = s;
            iconSize = 24 * s;
        }
        if (icon != null) {
            drawBitmap(canvas, icon, (inLeft + inRight - iconSize) / 2, y, iconScale, false);
        } else {
            waitingForIcons = true;
        }
        y += iconSize + 2 * s;

        GbaText.Fit name = GbaText.fit(gba.main, item.name, inRight - inLeft, lineBox(gba.main), s, s,
                Math.max(1, s - 1));
        GbaText.drawCentered(canvas, name, (inLeft + inRight - name.width) / 2, y + lineBox(gba.main) / 2,
                TEXT_DARK, SHADOW_DARK);
        y += lineBox(gba.main) + 2 * s;

        if (!item.move.isEmpty()) {
            TypeChart chart = typeChart();
            String typeName = chart != null ? chart.name(item.moveType) : "";
            int center = y + pillHeight() / 2;
            GbaText.Fit move = GbaText.fit(gba.small, item.move, (inRight - inLeft) * 6 / 10, pillHeight(), s, s,
                    Math.max(1, s - 1));
            int width = move.width + (typeName.isEmpty() ? 0
                    : gba.small[0].width(typeName) * s + 4 * s + 2 * s);
            int x = (inLeft + inRight - width) / 2;
            if (!typeName.isEmpty()) {
                x = drawPill(canvas, typeName, typeColor(typeName), Math.max(inLeft, x), center, inRight - inLeft)
                        + 2 * s;
            }
            GbaText.drawCentered(canvas, move, x, center, TEXT_DARK, SHADOW_DARK);
            y += pillHeight() + 2 * s;
        }

        rect(canvas, inLeft, y, inRight, y + s, SHADOW_DARK);
        y += 3 * s;
        drawWrapped(canvas, item.description, inLeft, y, inRight, bottom - (PAD_Y + 1) * s);
    }

    /** Small text broken into lines at spaces, as many lines as fit. */
    private void drawWrapped(Canvas canvas, String text, int left, int top, int right, int bottom) {
        GbaFont font = gba.small[0];
        int line = lineBox(gba.small) + s;
        int width = right - left;
        String[] words = text.trim().split("\\s+");
        StringBuilder current = new StringBuilder();
        int y = top;
        for (int i = 0; i <= words.length; i++) {
            String word = i < words.length ? words[i] : null;
            String candidate = word == null ? null
                    : current.length() == 0 ? word : current + " " + word;
            if (word != null && font.width(candidate) * s <= width) {
                current.setLength(0);
                current.append(candidate);
                continue;
            }
            if (current.length() > 0) {
                if (y + line > bottom) {
                    return;
                }
                GbaText.Fit fit = GbaText.fit(gba.small, current.toString(), width, line, s, s, Math.max(1, s - 1));
                GbaText.drawCentered(canvas, fit, left, y + line / 2, TEXT_DARK, SHADOW_DARK);
                y += line;
            }
            current.setLength(0);
            if (word != null) {
                current.append(word);
            }
        }
    }

    // ------------------------------------------------------------------
    // Pieces
    // ------------------------------------------------------------------

    /** Height of a pill: small text and a one-pixel border above and below. */
    private int pillHeight() {
        return lineBox(gba.small) + 2 * s;
    }

    /**
     * A tag in small capitals on a solid color (status, type, WILD/FOE).
     * Returns its right edge. The text shrinks or is cut to maxWidth.
     */
    private int drawPill(Canvas canvas, String text, int color, int left, int center, int maxWidth) {
        int height = pillHeight();
        GbaText.Fit fit = GbaText.fit(gba.small, text, Math.max(0, maxWidth - 4 * s), height - 2 * s,
                s, s, Math.max(1, s - 1));
        int right = left + fit.width + 4 * s;
        int top = center - height / 2;
        window(canvas, left, top, right, top + height, color, darker(color));
        int[] ink = onColor(color);
        GbaText.drawCentered(canvas, fit, left + 2 * s, center, ink[0], ink[1]);
        return right;
    }

    /** A plain light button with a label, for moving around the screen. */
    private void drawSmallButton(Canvas canvas, String label, int left, int top, int right, int bottom, int kind,
                                 int index, boolean enabled) {
        Button button = addButton(left, top, right, bottom, kind, index, enabled);
        boolean pressed = isPressed(button);
        window(canvas, left, top, right, bottom, pressed ? COLOR_ACTIVE_BORDER : COLOR_WINDOW,
                enabled ? COLOR_ACTIVE_BORDER : COLOR_WINDOW_BORDER);
        GbaText.Fit fit = GbaText.fit(gba.main, label, right - left - 2 * s, bottom - top, s, s, 1);
        int text = !enabled ? TEXT_DIM : pressed ? TEXT_LIGHT : TEXT_DARK;
        int shadow = !enabled ? SHADOW_DIM : pressed ? darker(COLOR_ACTIVE_BORDER) : SHADOW_DARK;
        GbaText.drawCentered(canvas, fit, (left + right - fit.width) / 2, (top + bottom) / 2, text, shadow);
    }

    /**
     * A GBA style bar: dark frame, a light line on top of the fill. HP
     * colors by how full it is, unless colors are given.
     */
    private void drawBar(Canvas canvas, int left, int center, int right, int value, int max, int[] colors) {
        int height = 6 * s;
        int top = center - height / 2;
        if (right - left < 4 * s) {
            return;
        }
        window(canvas, left, top, right, top + height, HP_TRACK, HP_FRAME);
        int innerLeft = left + s;
        int innerWidth = (right - s) - innerLeft;
        float fraction = max > 0 ? Math.max(0f, Math.min(1f, value / (float) max)) : 0f;
        if (fraction <= 0f) {
            return;
        }
        // Whole GBA pixels, and never less than one while any is left.
        int pixelCount = Math.max(1, Math.round(innerWidth / (float) s * fraction));
        int fillRight = Math.min(innerLeft + pixelCount * s, right - s);
        if (colors == null) {
            colors = fraction > 0.5f ? HP_GOOD : fraction > 0.2f ? HP_LOW : HP_CRITICAL;
        }
        rect(canvas, innerLeft, top + s, fillRight, top + height - s, colors[0]);
        rect(canvas, innerLeft, top + s, fillRight, top + 2 * s, colors[1]);
    }

    /**
     * A window with the game's look: a border one pixel thick and the four
     * corner pixels left out.
     */
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

    /** Greys out a control the game would not accept a touch on now. */
    private void dimOver(Canvas canvas, int left, int top, int right, int bottom) {
        rect(canvas, left, top, right, bottom, (COLOR_BACKGROUND & 0x00FFFFFF) | 0x90000000);
    }

    private static int[] ppColor(int pp, int maxPp) {
        if (pp == 0) {
            return new int[] { 0xFFE03820, 0xFFF8C0B0 };
        }
        if (pp * 4 <= maxPp) {
            return new int[] { 0xFFE07010, 0xFFF8D8A8 };
        }
        if (pp * 2 <= maxPp) {
            return new int[] { 0xFFB89000, 0xFFF0E0A0 };
        }
        return new int[] { TEXT_DARK, SHADOW_DARK };
    }

    private static int statusColor(String status) {
        switch (status) {
            case "PSN":
            case "TOX": return 0xFFB058B8;
            case "BRN": return 0xFFE87030;
            case "PAR": return 0xFFC8A818;
            case "SLP": return 0xFF8890A0;
            case "FRZ":
            case "FRB": return 0xFF58B0E0;
            default:    return 0xFFD04040; // FNT
        }
    }

    private static int typeColor(String type) {
        switch (type.toUpperCase(Locale.US)) {
            case "NORMAL":   return 0xFFA8A878;
            case "FIRE":     return 0xFFF08030;
            case "WATER":    return 0xFF6890F0;
            case "GRASS":    return 0xFF78C850;
            case "ELECTRIC": return 0xFFF8D030;
            case "ICE":      return 0xFF98D8D8;
            case "FIGHTING": return 0xFFC03028;
            case "POISON":   return 0xFFA040A0;
            case "GROUND":   return 0xFFE0C068;
            case "FLYING":   return 0xFFA890F0;
            case "PSYCHIC":  return 0xFFF85888;
            case "BUG":      return 0xFFA8B820;
            case "ROCK":     return 0xFFB8A038;
            case "GHOST":    return 0xFF705898;
            case "DRAGON":   return 0xFF7038F8;
            case "DARK":     return 0xFF705848;
            case "STEEL":    return 0xFFB8B8D0;
            case "FAIRY":    return 0xFFEE99AC;
            case "STELLAR":  return 0xFF40A8C8;
            default:         return 0xFF68A090; // ???
        }
    }

    /** Text and shadow colors that read on a solid background color. */
    private static int[] onColor(int background) {
        int r = (background >> 16) & 0xFF;
        int g = (background >> 8) & 0xFF;
        int b = background & 0xFF;
        int luma = (r * 3 + g * 6 + b) / 10;
        if (luma > 170) {
            return new int[] { 0xFF383838, mix(background, 0xFFFFFFFF, 50) };
        }
        return new int[] { TEXT_LIGHT, darker(background) };
    }

    private static int darker(int color) {
        return mix(color, 0xFF000000, 45);
    }

    /** color moved percent of the way towards other. */
    private static int mix(int color, int other, int percent) {
        int r = ((color >> 16) & 0xFF) + ((((other >> 16) & 0xFF) - ((color >> 16) & 0xFF)) * percent) / 100;
        int g = ((color >> 8) & 0xFF) + ((((other >> 8) & 0xFF) - ((color >> 8) & 0xFF)) * percent) / 100;
        int b = (color & 0xFF) + (((other & 0xFF) - (color & 0xFF)) * percent) / 100;
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    // ------------------------------------------------------------------
    // Touch
    // ------------------------------------------------------------------

    private Button addButton(float left, float top, float right, float bottom, int kind, int index, boolean enabled) {
        Button button = new Button();
        button.rect.set(left, top, right, bottom);
        button.kind = kind;
        button.index = index;
        button.enabled = enabled;
        buttons.add(button);
        return button;
    }

    private boolean isPressed(Button button) {
        return button.enabled && button.kind == pressedKind && button.index == pressedIndex;
    }

    private Button buttonAt(float x, float y) {
        // Last added first: buttons drawn on top of others (BACK on the
        // summary, the scroll arrows on the list) win.
        for (int i = buttons.size() - 1; i >= 0; i--) {
            Button button = buttons.get(i);
            if (button.enabled && button.rect.contains(x, y)) {
                return button;
            }
        }
        return null;
    }

    /** Does what a released touch on a button asks. */
    private void activate(Button button) {
        switch (button.kind) {
            case DualScreenBridge.TAP_MOVE:
            case DualScreenBridge.TAP_ACTION:
                try {
                    DualScreenBridge.nativeTap(button.kind, button.index);
                } catch (UnsatisfiedLinkError e) {
                    // The game library is not loaded. Nothing to drive.
                }
                break;
            case UI_TAB:
                tab = button.index;
                if (tab != TAB_BATTLE) {
                    tabBeforeBattle = tab;
                }
                updateBag();
                break;
            case UI_MON:
                detailSlot = button.index;
                break;
            case UI_BACK:
                detailSlot = -1;
                summaryMatchups = false;
                break;
            case UI_MATCHUPS:
                summaryMatchups = button.index == 1;
                break;
            case UI_PREVIOUS_MON:
            case UI_NEXT_MON:
                if (!state.party.isEmpty() && detailSlot >= 0) {
                    int step = button.kind == UI_NEXT_MON ? 1 : state.party.size() - 1;
                    detailSlot = (detailSlot + step) % state.party.size();
                }
                break;
            case UI_POCKET:
                pocket = button.index;
                bagSelected = 0;
                bagScroll = 0;
                break;
            case UI_ITEM:
                bagSelected = button.index;
                break;
            case UI_SCROLL:
                bagScroll += (button.index == 0 ? -1 : 1) * Math.max(1, bagRows - 1);
                break;
            default:
                break;
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: {
                Button button = buttonAt(event.getX(), event.getY());
                pressedKind = button != null ? button.kind : 0;
                pressedIndex = button != null ? button.index : 0;
                dragging = false;
                dragStartY = event.getY();
                dragStartScroll = bagScroll;
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                // A drag up or down the bag list scrolls it, a row at a time.
                if (tab == TAB_BAG && bagListRect.contains(event.getX(), dragStartY) && bagRowHeight > 0) {
                    float dy = event.getY() - dragStartY;
                    if (dragging || Math.abs(dy) > bagRowHeight / 2f) {
                        dragging = true;
                        pressedKind = 0;
                        int scroll = dragStartScroll - Math.round(dy / bagRowHeight);
                        if (scroll != bagScroll) {
                            bagScroll = scroll;
                            invalidate();
                        }
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_UP: {
                Button button = buttonAt(event.getX(), event.getY());
                // Only a touch that ends on the button it started on counts.
                if (!dragging && button != null && button.kind == pressedKind && button.index == pressedIndex) {
                    activate(button);
                }
                pressedKind = 0;
                dragging = false;
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                pressedKind = 0;
                dragging = false;
                invalidate();
                return true;
            default:
                return true;
        }
    }
}
