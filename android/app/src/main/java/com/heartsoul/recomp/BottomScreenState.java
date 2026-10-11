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

    /** Move categories, as the game numbers them. */
    static final int CATEGORY_PHYSICAL = 0;
    static final int CATEGORY_SPECIAL = 1;
    static final int CATEGORY_STATUS = 2;

    /** Stats in the summary screen's order. */
    static final int STAT_COUNT = 6;

    static final class Move {
        String name = "";
        String type = "";
        int typeId;
        int category = CATEGORY_STATUS;
        int power;
        int accuracy;
        int pp;
        int maxPp;
    }

    static final class Mon {
        String species = "";
        String nick = "";
        String status = "";
        String item = "";
        boolean egg;
        int speciesId;
        int iconSpecies;
        int personality;
        int[] types = new int[0];
        int level;
        int hp;
        int maxHp;
        final List<Move> moves = new ArrayList<>();

        /** Summary details, party mons only. */
        boolean hasDetails;
        boolean shiny;
        int gender = 0xFF;
        int friendship;
        String ability = "";
        String nature = "";
        int natureUp = -1;
        int natureDown = -1;
        int[] stats = new int[STAT_COUNT];
        int[] ivs = new int[STAT_COUNT];
        int[] evs = new int[STAT_COUNT];
        int exp;
        int expNext;

        String displayName() {
            return nick.isEmpty() ? species : nick;
        }
    }

    boolean inGame;
    boolean overworld;
    boolean fastForward;
    int frame;
    String playerName = "";
    String mapName = "";
    int money;
    int badges;
    int hours;
    int minutes;
    // The trainer card.
    int trainerId;
    int playerGender;
    boolean hasDex;
    boolean nationalDex;
    int dexSeen;
    int dexCaught;
    int badgeMask;
    int momMoney;
    int stars;
    int hofTime;
    final List<Mon> party = new ArrayList<>();

    boolean inBattle;
    boolean trainerBattle;
    int menu = MENU_NONE;
    int actionCursor;
    int moveCursor;
    int partyIndex = -1;
    int foeTeam;
    int foeTeamLeft;
    /** Trainer battles: the opponents' Pokémon (species, level, HP). */
    final List<Mon> foeParty = new ArrayList<>();
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
            state.fastForward = root.optBoolean("ff");
            state.frame = root.optInt("frame");

            JSONObject player = root.optJSONObject("player");
            if (player != null) {
                state.playerName = player.optString("name");
                state.mapName = player.optString("map");
                state.money = player.optInt("money");
                state.badges = player.optInt("badges");
                state.hours = player.optInt("hours");
                state.minutes = player.optInt("minutes");
                state.trainerId = player.optInt("id");
                state.playerGender = player.optInt("gender");
                state.hasDex = player.optBoolean("hasDex");
                state.nationalDex = player.optBoolean("nationalDex");
                state.dexSeen = player.optInt("dexSeen");
                state.dexCaught = player.optInt("dexCaught");
                state.badgeMask = player.optInt("badgeMask");
                state.momMoney = player.optInt("momMoney");
                state.stars = player.optInt("stars");
                state.hofTime = player.optInt("hofTime");
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
                state.foeTeam = battle.optInt("foeTeam");
                state.foeTeamLeft = battle.optInt("foeTeamLeft");
                JSONArray foeParty = battle.optJSONArray("foeParty");
                for (int i = 0; foeParty != null && i < foeParty.length(); i++) {
                    state.foeParty.add(parseMon(foeParty.getJSONObject(i)));
                }
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
        mon.speciesId = object.optInt("speciesId");
        mon.iconSpecies = object.optInt("iconSpecies", mon.speciesId);
        mon.personality = (int) object.optLong("personality");
        mon.types = intArray(object.optJSONArray("types"), 0);
        if (object.has("stats")) {
            mon.hasDetails = true;
            mon.shiny = object.optBoolean("shiny");
            mon.gender = object.optInt("gender", 0xFF);
            mon.friendship = object.optInt("friendship");
            mon.ability = object.optString("ability");
            mon.nature = object.optString("nature");
            mon.natureUp = object.optInt("natureUp", -1);
            mon.natureDown = object.optInt("natureDown", -1);
            mon.stats = intArray(object.optJSONArray("stats"), STAT_COUNT);
            mon.ivs = intArray(object.optJSONArray("ivs"), STAT_COUNT);
            mon.evs = intArray(object.optJSONArray("evs"), STAT_COUNT);
            mon.exp = object.optInt("exp");
            mon.expNext = object.optInt("expNext");
        }
        JSONArray moves = object.optJSONArray("moves");
        for (int i = 0; moves != null && i < moves.length(); i++) {
            JSONObject moveObject = moves.optJSONObject(i);
            if (moveObject == null) {
                continue;
            }
            Move move = new Move();
            move.name = moveObject.optString("name");
            move.type = moveObject.optString("type");
            move.typeId = moveObject.optInt("typeId");
            move.category = moveObject.optInt("cat", CATEGORY_STATUS);
            move.power = moveObject.optInt("power");
            move.accuracy = moveObject.optInt("acc");
            move.pp = moveObject.optInt("pp");
            move.maxPp = moveObject.optInt("maxPp");
            mon.moves.add(move);
        }
        return mon;
    }

    /** The array's numbers; at least minLength of them, padded with zeros. */
    static int[] intArray(JSONArray array, int minLength) {
        int length = array != null ? array.length() : 0;
        int[] result = new int[Math.max(length, minLength)];
        for (int i = 0; i < length; i++) {
            result[i] = array.optInt(i);
        }
        return result;
    }
}
