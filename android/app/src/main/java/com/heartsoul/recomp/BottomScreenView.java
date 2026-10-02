package com.heartsoul.recomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
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
 */
final class BottomScreenView extends View {
    private static final int COLOR_BACKGROUND = 0xFF101418;
    private static final int COLOR_PANEL = 0xFF1C232B;
    private static final int COLOR_PANEL_ACTIVE = 0xFF26323E;
    private static final int COLOR_OUTLINE = 0xFF34404C;
    private static final int COLOR_TEXT = 0xFFF2F4F6;
    private static final int COLOR_TEXT_DIM = 0xFF8C98A4;
    private static final int COLOR_ACCENT = 0xFFFFC83C;
    private static final int COLOR_HP_GOOD = 0xFF4CC864;
    private static final int COLOR_HP_LOW = 0xFFE8C030;
    private static final int COLOR_HP_CRITICAL = 0xFFE04838;
    private static final int COLOR_HP_TRACK = 0xFF0A0D10;

    private static final String[] TYPE_KEYS = {
        "NOR", "FIR", "WAT", "GRA", "ELE", "ICE", "FIG", "POI", "GRO",
        "FLY", "PSY", "BUG", "ROC", "GHO", "DRA", "DAR", "STE", "FAI",
    };
    private static final int[] TYPE_COLORS = {
        0xFFA8A878, 0xFFF08030, 0xFF6890F0, 0xFF78C850, 0xFFF8D030, 0xFF98D8D8, 0xFFC03028, 0xFFA040A0, 0xFFE0C068,
        0xFFA890F0, 0xFFF85888, 0xFFA8B820, 0xFFB8A038, 0xFF705898, 0xFF7038F8, 0xFF705848, 0xFFB8B8D0, 0xFFEE99AC,
    };

    /** A touch target laid out by the last draw. */
    private static final class Button {
        final RectF rect = new RectF();
        int kind;
        int index;
        boolean enabled;
    }

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final List<Button> buttons = new ArrayList<>();

    private BottomScreenState state = new BottomScreenState();
    private int pressedKind;
    private int pressedIndex;

    BottomScreenView(Context context) {
        super(context);
        stroke.setStyle(Paint.Style.STROKE);
        text.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
    }

    void setState(BottomScreenState newState) {
        state = newState;
        invalidate();
    }

    // ------------------------------------------------------------------
    // Drawing
    // ------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        float width = getWidth();
        float height = getHeight();
        float unit = Math.min(width, height) / 40f;
        float pad = unit;

        canvas.drawColor(COLOR_BACKGROUND);
        buttons.clear();
        stroke.setStrokeWidth(Math.max(2f, unit * 0.15f));

        if (!state.inGame) {
            text.setColor(COLOR_TEXT);
            text.setTextAlign(Paint.Align.CENTER);
            text.setTextSize(unit * 3f);
            canvas.drawText("Pokémon Heart & Soul", width / 2f, height / 2f - unit, text);
            text.setColor(COLOR_TEXT_DIM);
            text.setTextSize(unit * 1.8f);
            canvas.drawText("Your party shows here once the game is loaded.", width / 2f, height / 2f + unit * 2f, text);
            return;
        }

        float headerBottom = drawHeader(canvas, pad, pad, width - pad, unit);
        float top = headerBottom + pad;
        float bottom = height - pad;

        if (state.inBattle) {
            float partyRight = pad + (width - pad * 3f) * 0.36f;
            drawPartyColumn(canvas, pad, top, partyRight, bottom, unit);
            drawBattle(canvas, partyRight + pad, top, width - pad, bottom, unit);
        } else {
            drawPartyGrid(canvas, pad, top, width - pad, bottom, unit);
        }
    }

    private float drawHeader(Canvas canvas, float left, float top, float right, float unit) {
        float bottom = top + unit * 3.6f;
        float baseline = bottom - unit * 1.2f;

        panel(canvas, left, top, right, bottom, COLOR_PANEL, unit);
        text.setTextSize(unit * 1.9f);

        String rightText = String.format(Locale.US, "¥%d   Badges %d   %d:%02d",
                state.money, state.badges, state.hours, state.minutes);
        text.setColor(COLOR_TEXT_DIM);
        text.setTextAlign(Paint.Align.RIGHT);
        canvas.drawText(rightText, right - unit, baseline, text);
        float rightWidth = text.measureText(rightText);

        text.setColor(COLOR_TEXT);
        String leftText = state.mapName.isEmpty() ? state.playerName : state.playerName + "  ·  " + state.mapName;
        drawFitted(canvas, leftText, left + unit, baseline, right - left - rightWidth - unit * 4f, unit * 1.9f);
        return bottom;
    }

    /** Out of battle: six cards, two across. */
    private void drawPartyGrid(Canvas canvas, float left, float top, float right, float bottom, float unit) {
        float gap = unit;
        float cardWidth = (right - left - gap) / 2f;
        float cardHeight = (bottom - top - gap * 2f) / 3f;

        for (int i = 0; i < 6; i++) {
            float x = left + (i % 2) * (cardWidth + gap);
            float y = top + (i / 2) * (cardHeight + gap);
            BottomScreenState.Mon mon = i < state.party.size() ? state.party.get(i) : null;
            drawMonCard(canvas, mon, x, y, x + cardWidth, y + cardHeight, unit, false, true);
        }
    }

    /** In battle: the party as a narrow column beside the buttons. */
    private void drawPartyColumn(Canvas canvas, float left, float top, float right, float bottom, float unit) {
        float gap = unit * 0.6f;
        float cardHeight = (bottom - top - gap * 5f) / 6f;

        for (int i = 0; i < 6; i++) {
            float y = top + i * (cardHeight + gap);
            BottomScreenState.Mon mon = i < state.party.size() ? state.party.get(i) : null;
            drawMonCard(canvas, mon, left, y, right, y + cardHeight, unit, i == state.partyIndex, false);
        }
    }

    private void drawMonCard(Canvas canvas, BottomScreenState.Mon mon, float left, float top, float right, float bottom,
                             float unit, boolean active, boolean roomy) {
        float inset = unit * 0.9f;
        float cardHeight = bottom - top;
        float nameSize = Math.min(unit * (roomy ? 2.4f : 1.8f), cardHeight * 0.34f);
        float smallSize = nameSize * 0.75f;

        if (mon == null) {
            rect.set(left, top, right, bottom);
            stroke.setColor(COLOR_OUTLINE);
            canvas.drawRoundRect(rect, unit * 0.8f, unit * 0.8f, stroke);
            return;
        }

        panel(canvas, left, top, right, bottom, active ? COLOR_PANEL_ACTIVE : COLOR_PANEL, unit);
        if (active) {
            rect.set(left, top, right, bottom);
            stroke.setColor(COLOR_ACCENT);
            canvas.drawRoundRect(rect, unit * 0.8f, unit * 0.8f, stroke);
        }

        float nameBaseline = top + inset + nameSize * 0.85f;
        if (mon.egg) {
            text.setColor(COLOR_TEXT);
            text.setTextAlign(Paint.Align.LEFT);
            text.setTextSize(nameSize);
            canvas.drawText("Egg", left + inset, nameBaseline, text);
            return;
        }

        // Level on the right, then the name in what is left of the line.
        String level = "Lv " + mon.level;
        text.setTextSize(smallSize);
        text.setColor(COLOR_TEXT_DIM);
        text.setTextAlign(Paint.Align.RIGHT);
        canvas.drawText(level, right - inset, nameBaseline, text);
        float levelWidth = text.measureText(level);

        text.setColor(mon.hp == 0 ? COLOR_TEXT_DIM : COLOR_TEXT);
        drawFitted(canvas, mon.displayName(), left + inset, nameBaseline,
                right - left - inset * 2f - levelWidth - unit, nameSize);

        float barTop = nameBaseline + cardHeight * 0.10f;
        float barHeight = Math.max(unit * 0.7f, cardHeight * 0.12f);
        drawHpBar(canvas, left + inset, barTop, right - inset, barTop + barHeight, mon.hp, mon.maxHp);

        float infoBaseline = barTop + barHeight + smallSize * 1.15f;
        if (infoBaseline > bottom - inset * 0.4f) {
            return; // no room for the third line
        }
        text.setTextSize(smallSize);
        text.setTextAlign(Paint.Align.LEFT);
        text.setColor(COLOR_TEXT);
        String hp = mon.hp + " / " + mon.maxHp;
        canvas.drawText(hp, left + inset, infoBaseline, text);
        float x = left + inset + text.measureText(hp) + unit;
        if (!mon.status.isEmpty()) {
            text.setColor(statusColor(mon.status));
            canvas.drawText(mon.status, x, infoBaseline, text);
            x += text.measureText(mon.status) + unit;
        }
        if (roomy && !mon.item.isEmpty()) {
            text.setColor(COLOR_TEXT_DIM);
            text.setTextAlign(Paint.Align.RIGHT);
            drawFittedRight(canvas, mon.item, right - inset, infoBaseline, right - inset - x, smallSize);
        }

        // With room to spare, list the moves under the card's three lines.
        if (roomy) {
            float moveSize = smallSize * 0.9f;
            float moveBaseline = infoBaseline + moveSize * 1.6f;
            float columnWidth = (right - left - inset * 2f) / 2f;
            for (int i = 0; i < mon.moves.size() && i < 4; i++) {
                float y = moveBaseline + (i / 2) * moveSize * 1.4f;
                if (y > bottom - inset * 0.5f) {
                    break;
                }
                BottomScreenState.Move move = mon.moves.get(i);
                text.setColor(COLOR_TEXT_DIM);
                text.setTextAlign(Paint.Align.LEFT);
                drawFitted(canvas, move.name + "  " + move.pp + "/" + move.maxPp,
                        left + inset + (i % 2) * columnWidth, y, columnWidth - unit, moveSize);
            }
        }
    }

    private void drawBattle(Canvas canvas, float left, float top, float right, float bottom, float unit) {
        float gap = unit;
        float foeHeight = unit * 6f;
        float actionHeight = (bottom - top) * 0.17f;
        float movesTop = top + foeHeight + gap;
        float movesBottom = bottom - actionHeight - gap;
        boolean canChoose = state.menu == BottomScreenState.MENU_ACTION || state.menu == BottomScreenState.MENU_MOVE;

        // The opposing Pokémon. Its HP is shown as a bar only, as in the game.
        panel(canvas, left, top, right, top + foeHeight, COLOR_PANEL, unit);
        float inset = unit;
        if (state.foe != null) {
            float baseline = top + inset + unit * 2f;
            String level = "Lv " + state.foe.level;
            text.setTextSize(unit * 1.7f);
            text.setColor(COLOR_TEXT_DIM);
            text.setTextAlign(Paint.Align.RIGHT);
            canvas.drawText(level, right - inset, baseline, text);
            float used = text.measureText(level) + unit;
            if (!state.foe.status.isEmpty()) {
                text.setColor(statusColor(state.foe.status));
                canvas.drawText(state.foe.status, right - inset - used, baseline, text);
                used += text.measureText(state.foe.status) + unit;
            }
            text.setColor(COLOR_TEXT);
            String name = (state.trainerBattle ? "Foe " : "Wild ") + state.foe.displayName();
            drawFitted(canvas, name, left + inset, baseline, right - left - inset * 2f - used, unit * 2.3f);
            drawHpBar(canvas, left + inset, baseline + unit * 0.9f, right - inset, baseline + unit * 1.9f,
                    state.foe.hp, state.foe.maxHp);
        }

        // Moves, in the game's own cursor order: 0 1 / 2 3.
        float moveWidth = (right - left - gap) / 2f;
        float moveHeight = (movesBottom - movesTop - gap) / 2f;
        for (int i = 0; i < 4; i++) {
            float x = left + (i % 2) * (moveWidth + gap);
            float y = movesTop + (i / 2) * (moveHeight + gap);
            BottomScreenState.Move move = null;
            if (state.battleMon != null && i < state.battleMon.moves.size()) {
                move = state.battleMon.moves.get(i);
            }
            drawMoveButton(canvas, move, i, x, y, x + moveWidth, y + moveHeight, unit, canChoose);
        }

        // Bag, Pokémon, Run.
        String[] labels = { "Bag", "Pokémon", "Run" };
        int[] actions = { DualScreenBridge.ACTION_BAG, DualScreenBridge.ACTION_POKEMON, DualScreenBridge.ACTION_RUN };
        float actionWidth = (right - left - gap * 2f) / 3f;
        for (int i = 0; i < 3; i++) {
            float x = left + i * (actionWidth + gap);
            Button button = addButton(x, bottom - actionHeight, x + actionWidth, bottom,
                    DualScreenBridge.TAP_ACTION, actions[i], canChoose);
            boolean pressed = isPressed(button);
            panel(canvas, button.rect.left, button.rect.top, button.rect.right, button.rect.bottom,
                    pressed ? COLOR_ACCENT : COLOR_PANEL_ACTIVE, unit);
            text.setColor(pressed ? COLOR_BACKGROUND : canChoose ? COLOR_TEXT : COLOR_TEXT_DIM);
            text.setTextAlign(Paint.Align.CENTER);
            float size = Math.min(unit * 2.4f, actionHeight * 0.4f);
            text.setTextSize(size);
            canvas.drawText(labels[i], button.rect.centerX(), button.rect.centerY() + size * 0.35f, text);
        }

        if (state.menu == BottomScreenState.MENU_TARGET) {
            text.setColor(COLOR_ACCENT);
            text.setTextAlign(Paint.Align.CENTER);
            text.setTextSize(unit * 1.6f);
            canvas.drawText("Choose the target with the buttons", (left + right) / 2f, movesTop - gap * 0.2f, text);
        }
    }

    private void drawMoveButton(Canvas canvas, BottomScreenState.Move move, int slot, float left, float top, float right,
                                float bottom, float unit, boolean canChoose) {
        if (move == null) {
            rect.set(left, top, right, bottom);
            stroke.setColor(COLOR_OUTLINE);
            canvas.drawRoundRect(rect, unit * 0.8f, unit * 0.8f, stroke);
            return;
        }

        boolean enabled = canChoose;
        Button button = addButton(left, top, right, bottom, DualScreenBridge.TAP_MOVE, slot, enabled);
        boolean pressed = isPressed(button);
        int typeColor = typeColor(move.type);
        float inset = unit * 1.2f;
        float height = bottom - top;

        panel(canvas, left, top, right, bottom, pressed ? COLOR_ACCENT : COLOR_PANEL_ACTIVE, unit);
        // A band in the type's color down the left edge.
        fill.setColor(enabled ? typeColor : dim(typeColor));
        rect.set(left, top, left + unit * 1.1f, bottom);
        canvas.drawRoundRect(rect, unit * 0.5f, unit * 0.5f, fill);

        float nameSize = Math.min(unit * 2.8f, height * 0.26f);
        float smallSize = nameSize * 0.68f;
        float textLeft = left + unit * 1.1f + inset;
        int mainColor = pressed ? COLOR_BACKGROUND : (enabled && move.pp > 0) ? COLOR_TEXT : COLOR_TEXT_DIM;

        text.setColor(mainColor);
        drawFitted(canvas, move.name, textLeft, top + height * 0.42f, right - inset - textLeft, nameSize);

        text.setTextSize(smallSize);
        text.setTextAlign(Paint.Align.LEFT);
        text.setColor(pressed ? COLOR_BACKGROUND : enabled ? typeColor : dim(typeColor));
        canvas.drawText(move.type, textLeft, top + height * 0.74f, text);
        text.setTextAlign(Paint.Align.RIGHT);
        text.setColor(pressed ? COLOR_BACKGROUND : move.pp == 0 ? COLOR_HP_CRITICAL : mainColor);
        canvas.drawText("PP " + move.pp + "/" + move.maxPp, right - inset, top + height * 0.74f, text);

        // Where the game's own cursor is, so both screens agree.
        if (state.menu == BottomScreenState.MENU_MOVE && slot == state.moveCursor && !pressed) {
            rect.set(left, top, right, bottom);
            stroke.setColor(COLOR_ACCENT);
            canvas.drawRoundRect(rect, unit * 0.8f, unit * 0.8f, stroke);
        }
    }

    private void drawHpBar(Canvas canvas, float left, float top, float right, float bottom, int hp, int maxHp) {
        float radius = (bottom - top) / 2f;
        float fraction = maxHp > 0 ? Math.max(0f, Math.min(1f, hp / (float) maxHp)) : 0f;

        fill.setColor(COLOR_HP_TRACK);
        rect.set(left, top, right, bottom);
        canvas.drawRoundRect(rect, radius, radius, fill);
        if (fraction <= 0f) {
            return;
        }
        fill.setColor(fraction > 0.5f ? COLOR_HP_GOOD : fraction > 0.2f ? COLOR_HP_LOW : COLOR_HP_CRITICAL);
        // Never narrower than the rounded end, so 1 HP is still visible.
        rect.set(left, top, Math.max(left + radius * 2f, left + (right - left) * fraction), bottom);
        canvas.drawRoundRect(rect, radius, radius, fill);
    }

    private void panel(Canvas canvas, float left, float top, float right, float bottom, int color, float unit) {
        fill.setColor(color);
        rect.set(left, top, right, bottom);
        canvas.drawRoundRect(rect, unit * 0.8f, unit * 0.8f, fill);
    }

    /** Left-aligned text, shrunk to fit maxWidth. Uses the current text color. */
    private void drawFitted(Canvas canvas, String value, float x, float baseline, float maxWidth, float size) {
        text.setTextAlign(Paint.Align.LEFT);
        fitTextSize(value, maxWidth, size);
        canvas.drawText(value, x, baseline, text);
    }

    private void drawFittedRight(Canvas canvas, String value, float right, float baseline, float maxWidth, float size) {
        text.setTextAlign(Paint.Align.RIGHT);
        fitTextSize(value, maxWidth, size);
        canvas.drawText(value, right, baseline, text);
    }

    private void fitTextSize(String value, float maxWidth, float size) {
        text.setTextSize(size);
        float measured = text.measureText(value);
        if (maxWidth > 0f && measured > maxWidth) {
            text.setTextSize(Math.max(size * 0.5f, size * maxWidth / measured));
        }
    }

    private static int statusColor(String status) {
        switch (status) {
            case "PSN":
            case "TOX": return 0xFFC060E0;
            case "BRN": return 0xFFF08030;
            case "PAR": return 0xFFF8D030;
            case "SLP": return 0xFFA0A8B0;
            case "FRZ":
            case "FRB": return 0xFF98D8D8;
            default:    return COLOR_HP_CRITICAL; // FNT
        }
    }

    private static int typeColor(String type) {
        String key = type.toUpperCase(Locale.US);
        if (key.length() >= 3) {
            key = key.substring(0, 3);
            for (int i = 0; i < TYPE_KEYS.length; i++) {
                if (TYPE_KEYS[i].equals(key)) {
                    return TYPE_COLORS[i];
                }
            }
        }
        return COLOR_TEXT_DIM;
    }

    /** The same color at half strength over the dark background. */
    private static int dim(int color) {
        int r = ((color >> 16) & 0xFF) / 2;
        int g = ((color >> 8) & 0xFF) / 2;
        int b = (color & 0xFF) / 2;
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
