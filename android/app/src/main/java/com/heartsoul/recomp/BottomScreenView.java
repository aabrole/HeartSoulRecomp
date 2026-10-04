package com.heartsoul.recomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Draws the companion screen: the player line and the party, and during a
 * battle the opposing Pokémon, the four moves and Bag / Pokémon / Run as
 * touch buttons. A touch only sends a request to the game. What is on
 * screen changes when the next snapshot says the game did.
 *
 * Everything is drawn GBA style: text in the game's own fonts (GbaText),
 * flat colors, borders one scaled pixel thick. One number sets every size:
 * the pixel scale s, the largest whole scale at which a 300x240 pixel
 * design fits the view (4 on the Thor's 1240x1080 bottom screen, 2 on the
 * RG DS's 640x480; 250 high would leave the RG DS at 1, too small). Margins,
 * gaps, borders and text are whole multiples of s; panels share out what is
 * left of the view, so the layout fills any size. Each string is fitted to
 * the box it was given (see GbaText), so no text overlaps or leaves its box.
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

    private static final int TEXT_DARK = 0xFF505058;
    private static final int SHADOW_DARK = 0xFFD0D0C8;
    private static final int TEXT_DIM = 0xFF989898;
    private static final int SHADOW_DIM = 0xFFE0E0D8;
    private static final int TEXT_LIGHT = 0xFFF8F8F8;
    private static final int SHADOW_LIGHT = 0xFF606878;
    private static final int TEXT_ACCENT = 0xFFF8D058;
    private static final int SHADOW_ACCENT = 0xFF806020;

    private static final int HP_FRAME = 0xFF404858;
    private static final int HP_TRACK = 0xFF586070;
    private static final int[] HP_GOOD = { 0xFF58D080, 0xFF88F8B0 };
    private static final int[] HP_LOW = { 0xFFE8B020, 0xFFF8E070 };
    private static final int[] HP_CRITICAL = { 0xFFE84830, 0xFFF89070 };

    private static final int COLOR_BAG = 0xFFE8A030;
    private static final int COLOR_POKEMON = 0xFF50B060;
    private static final int COLOR_RUN = 0xFF4888E0;

    /** A touch target laid out by the last draw. */
    private static final class Button {
        final RectF rect = new RectF();
        int kind;
        int index;
        boolean enabled;
    }

    private final Paint fill = new Paint();
    private final Paint fallbackText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final List<Button> buttons = new ArrayList<>();
    private final GbaText gba;

    private BottomScreenState state = new BottomScreenState();
    private int pressedKind;
    private int pressedIndex;

    /** The pixel scale of the current draw. */
    private int s = 1;

    BottomScreenView(Context context) {
        super(context);
        fill.setAntiAlias(false);
        gba = GbaText.load(context.getAssets());
    }

    void setState(BottomScreenState newState) {
        state = newState;
        invalidate();
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
        int top = headerBottom + gap;
        int bottom = height - margin;

        if (state.inBattle) {
            // The party is a reference column; the battle gets the larger share.
            int partyRight = margin + (width - 2 * margin - gap) * 34 / 100;
            drawPartyColumn(canvas, margin, top, partyRight, bottom);
            drawBattle(canvas, partyRight + gap, top, width - margin, bottom);
        } else {
            drawPartyGrid(canvas, margin, top, width - margin, bottom);
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

    /** Player and map on the left; money, badges and play time on the right. */
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

        int nameRoom = inner - infoFit.width - 2 * GAP * s;
        GbaText.Fit player = GbaText.fit(gba.main, state.playerName, nameRoom, maxHeight, s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, player, inLeft, center, TEXT_ACCENT, SHADOW_ACCENT);
        if (!state.mapName.isEmpty()) {
            int mapLeft = inLeft + player.width + (player.width > 0 ? GAP * s : 0);
            GbaText.Fit map = GbaText.fit(gba.main, state.mapName, inLeft + nameRoom - mapLeft, maxHeight,
                    s, s, Math.max(1, s - 1));
            GbaText.drawCentered(canvas, map, mapLeft, center, TEXT_LIGHT, SHADOW_LIGHT);
        }
    }

    /** Out of battle: six cards, two across, three down. */
    private void drawPartyGrid(Canvas canvas, int left, int top, int right, int bottom) {
        int gap = GAP * s;
        int cardWidth = (right - left - gap) / 2;
        int cardHeight = (bottom - top - 2 * gap) / 3;
        for (int i = 0; i < 6; i++) {
            int x = left + (i % 2) * (cardWidth + gap);
            int y = top + (i / 2) * (cardHeight + gap);
            BottomScreenState.Mon mon = i < state.party.size() ? state.party.get(i) : null;
            drawMonCard(canvas, mon, x, y, x + cardWidth, y + cardHeight, false, true);
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
     * A party member. The card shows as many of these rows as fit, in this
     * order, and spreads them over its height: name and level; status or
     * "HP", HP bar and HP numbers; held item; the four moves.
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
        int nameRow = lineBox(gba.main);
        int smallRow = pillHeight();
        int minGap = 2 * s;

        if (mon.egg) {
            GbaText.Fit egg = GbaText.fit(gba.main, "EGG", inRight - inLeft, inBottom - inTop, s, s, 1);
            GbaText.drawCentered(canvas, egg, inLeft, (inTop + inBottom) / 2, TEXT_DARK, SHADOW_DARK);
            return;
        }

        // Rows that fit, in priority order.
        int[] heights = new int[5];
        int rows = 0;
        int used = 0;
        int available = inBottom - inTop;
        int[] wanted = { nameRow, smallRow, smallRow, smallRow, smallRow };
        int maxRows = withMoves ? 5 : 3;
        for (int i = 0; i < maxRows; i++) {
            int need = used + wanted[i] + (rows > 0 ? minGap : 0);
            if (need > available && rows > 0) {
                break;
            }
            heights[rows++] = wanted[i];
            used = need;
        }
        // Moves come in pairs: one row of moves alone would hide two of them.
        if (rows == 4) {
            rows = 3;
        }
        int contentHeight = 0;
        for (int i = 0; i < rows; i++) {
            contentHeight += heights[i];
        }
        int gap = rows > 1 ? Math.min((available - contentHeight) / (rows - 1), 6 * s) : 0;
        int y = inTop + (available - contentHeight - gap * (rows - 1)) / 2;

        for (int i = 0; i < rows; i++) {
            int center = y + heights[i] / 2;
            switch (i) {
                case 0:
                    drawNameRow(canvas, mon, inLeft, inRight, center, heights[i], fainted);
                    break;
                case 1:
                    drawHpRow(canvas, mon, inLeft, inRight, center, true);
                    break;
                case 2:
                    drawItemRow(canvas, mon, inLeft, inRight, center);
                    break;
                case 3:
                    drawMoveList(canvas, mon, inLeft, inRight, y, heights[3], gap + heights[4]);
                    break;
                default:
                    break;
            }
            y += heights[i] + gap;
        }
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
        drawHpBar(canvas, x, center, barRight, mon.hp, mon.maxHp);
    }

    /** A small bag symbol and the held item's name. */
    private void drawItemRow(Canvas canvas, BottomScreenState.Mon mon, int left, int right, int center) {
        if (mon.item.isEmpty()) {
            return;
        }
        // The held-item mark: a 6x6 bag with a darker band.
        int size = 6 * s;
        int top = center - size / 2;
        window(canvas, left, top, left + size, top + size, 0xFFF89078, 0xFF984838);
        rect(canvas, left + s, top + 2 * s, left + size - s, top + 3 * s, 0xFFC86050);
        int x = left + size + 2 * s;
        GbaText.Fit item = GbaText.fit(gba.small, mon.item, right - x, pillHeight(), s, s, Math.max(1, s - 1));
        GbaText.drawCentered(canvas, item, x, center, TEXT_DARK, SHADOW_DARK);
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
    // Battle
    // ------------------------------------------------------------------

    private void drawBattle(Canvas canvas, int left, int top, int right, int bottom) {
        int gap = GAP * s;
        boolean canChoose = state.menu == BottomScreenState.MENU_ACTION || state.menu == BottomScreenState.MENU_MOVE;

        // The foe: a name line and an HP bar line.
        int foeBottom = top + lineBox(gba.main) + pillHeight() + (2 * (PAD_Y + 1) + 3) * s;
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
        int nameHeight = moveHeight - pillHeight() - 2 * (PAD_Y + 1) * s - 2 * s;
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
        int barCenter = bottom - (PAD_Y + 1) * s - pillHeight() / 2;

        // "WILD" or "FOE" as a tag, then the name and level as in a card.
        int x = drawPill(canvas, state.trainerBattle ? "FOE" : "WILD", COLOR_WINDOW_BORDER, inLeft, nameCenter,
                (inRight - inLeft) / 4) + 3 * s;
        drawNameRow(canvas, foe, x, inRight, nameCenter, lineBox(gba.main), false);
        // The game shows no HP numbers for the foe, only the bar.
        drawHpRow(canvas, foe, inLeft, inRight, barCenter, false);
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

        // The name, centred in the space above.
        int nameTop = top + (PAD_Y + 1) * s;
        GbaText.Fit name = GbaText.fit(gba.main, move.name, inRight - inLeft, infoTop - nameTop - s,
                nameScale, nameScale, nameScale);
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

    /** A GBA style HP bar: dark frame, a light line on top of the fill. */
    private void drawHpBar(Canvas canvas, int left, int center, int right, int hp, int maxHp) {
        int height = 6 * s;
        int top = center - height / 2;
        if (right - left < 4 * s) {
            return;
        }
        window(canvas, left, top, right, top + height, HP_TRACK, HP_FRAME);
        int innerLeft = left + s;
        int innerWidth = (right - s) - innerLeft;
        float fraction = maxHp > 0 ? Math.max(0f, Math.min(1f, hp / (float) maxHp)) : 0f;
        if (fraction <= 0f) {
            return;
        }
        // Whole GBA pixels, and never less than one while any HP is left.
        int pixels = Math.max(1, Math.round(innerWidth / (float) s * fraction));
        int fillRight = Math.min(innerLeft + pixels * s, right - s);
        int[] colors = fraction > 0.5f ? HP_GOOD : fraction > 0.2f ? HP_LOW : HP_CRITICAL;
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
        for (Button button : buttons) {
            if (button.enabled && button.rect.contains(x, y)) {
                return button;
            }
        }
        return null;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: {
                Button button = buttonAt(event.getX(), event.getY());
                pressedKind = button != null ? button.kind : 0;
                pressedIndex = button != null ? button.index : 0;
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP: {
                Button button = buttonAt(event.getX(), event.getY());
                // Only a touch that ends on the button it started on counts.
                if (button != null && button.kind == pressedKind && button.index == pressedIndex) {
                    try {
                        DualScreenBridge.nativeTap(button.kind, button.index);
                    } catch (UnsatisfiedLinkError e) {
                        // The game library is not loaded. Nothing to drive.
                    }
                }
                pressedKind = 0;
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                pressedKind = 0;
                invalidate();
                return true;
            default:
                return true;
        }
    }
}
