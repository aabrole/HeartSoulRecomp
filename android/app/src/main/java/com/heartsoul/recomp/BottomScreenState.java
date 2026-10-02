package com.heartsoul.recomp;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/** One parsed snapshot of the game state. Not changed after parsing. */
final class BottomScreenState {
    static final int MENU_NONE = 0;
    static final int MENU_ACTION = 1;
    static final int MENU_MOVE = 2;
    static final int MENU_TARGET = 3;

    static final class Move {
        String name = "";
        String type = "";
        int pp;
        int maxPp;
    }

    static final class Mon {
        String species = "";
        String nick = "";
        String status = "";
        String item = "";
        boolean egg;
        int level;
        int hp;
        int maxHp;
        final List<Move> moves = new ArrayList<>();

        String displayName() {
            return nick.isEmpty() ? species : nick;
        }
    }

    boolean inGame;
    boolean overworld;
    String playerName = "";
    String mapName = "";
    int money;
    int badges;
    int hours;
    int minutes;
    final List<Mon> party = new ArrayList<>();

    boolean inBattle;
    boolean trainerBattle;
    int menu = MENU_NONE;
    int actionCursor;
    int moveCursor;
    int partyIndex = -1;
    Mon battleMon;
    Mon foe;

    /** Returns an empty, not-in-game state for text that cannot be parsed. */
    static BottomScreenState parse(String json) {
        BottomScreenState state = new BottomScreenState();
        if (json == null || json.isEmpty()) {
            return state;
        }
        try {
            JSONObject root = new JSONObject(json);
            state.inGame = root.optBoolean("inGame");
            state.overworld = root.optBoolean("overworld");

            JSONObject player = root.optJSONObject("player");
            if (player != null) {
                state.playerName = player.optString("name");
                state.mapName = player.optString("map");
                state.money = player.optInt("money");
                state.badges = player.optInt("badges");
                state.hours = player.optInt("hours");
                state.minutes = player.optInt("minutes");
            }

            JSONArray party = root.optJSONArray("party");
            for (int i = 0; party != null && i < party.length(); i++) {
                state.party.add(parseMon(party.getJSONObject(i)));
            }

            JSONObject battle = root.optJSONObject("battle");
            if (battle != null) {
                state.inBattle = true;
                state.trainerBattle = battle.optBoolean("trainer");
                state.menu = parseMenu(battle.optString("menu"));
                state.actionCursor = battle.optInt("actionCursor");
                state.moveCursor = battle.optInt("moveCursor");
                state.partyIndex = battle.optInt("partyIndex", -1);
                JSONObject mon = battle.optJSONObject("mon");
                if (mon != null) {
                    state.battleMon = parseMon(mon);
                }
                JSONObject foe = battle.optJSONObject("foe");
                if (foe != null) {
                    state.foe = parseMon(foe);
                }
            }
        } catch (JSONException e) {
            return new BottomScreenState();
        }
        return state;
    }

    private static int parseMenu(String name) {
        switch (name) {
            case "action": return MENU_ACTION;
            case "move":   return MENU_MOVE;
            case "target": return MENU_TARGET;
            default:       return MENU_NONE;
        }
    }

    private static Mon parseMon(JSONObject object) {
        Mon mon = new Mon();
        mon.species = object.optString("species");
        mon.nick = object.optString("nick");
        mon.status = object.optString("status");
        mon.item = object.optString("item");
        mon.egg = object.optBoolean("egg");
        mon.level = object.optInt("lv");
        mon.hp = object.optInt("hp");
        mon.maxHp = object.optInt("maxHp");
        JSONArray moves = object.optJSONArray("moves");
        for (int i = 0; moves != null && i < moves.length(); i++) {
            JSONObject moveObject = moves.optJSONObject(i);
            if (moveObject == null) {
                continue;
            }
            Move move = new Move();
            move.name = moveObject.optString("name");
            move.type = moveObject.optString("type");
            move.pp = moveObject.optInt("pp");
            move.maxPp = moveObject.optInt("maxPp");
            mon.moves.add(move);
        }
        return mon;
    }
}
