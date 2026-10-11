package com.heartsoul.recomp;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * The game's type chart (gTypeEffectivenessTable), read once from the game.
 * Multipliers are kept as the game keeps them, in 1/4096ths, so 4096 is
 * neutral, 8192 super effective and 0 no effect.
 */
final class TypeChart {
    static final int NEUTRAL = 4096;
    static final int TYPE_NONE = 0;
    static final int TYPE_MYSTERY = 10;
    static final int TYPE_STELLAR = 20;

    final String[] names;
    private final int[][] chart;

    private TypeChart(String[] names, int[][] chart) {
        this.names = names;
        this.chart = chart;
    }

    /** Null for text that is not a whole chart. */
    static TypeChart parse(String json) {
        if (json == null || json.isEmpty()) {
            return null;
        }
        try {
            JSONObject root = new JSONObject(json);
            JSONArray nameArray = root.getJSONArray("names");
            JSONArray rows = root.getJSONArray("chart");
            int count = nameArray.length();
            if (count == 0 || rows.length() != count) {
                return null;
            }
            String[] names = new String[count];
            int[][] chart = new int[count][];
            for (int i = 0; i < count; i++) {
                names[i] = nameArray.getString(i);
                chart[i] = BottomScreenState.intArray(rows.getJSONArray(i), count);
            }
            return new TypeChart(names, chart);
        } catch (JSONException e) {
            return null;
        }
    }

    boolean isType(int type) {
        return type > TYPE_NONE && type < names.length && type != TYPE_MYSTERY;
    }

    String name(int type) {
        return type >= 0 && type < names.length ? names[type] : "";
    }

    /** An attack of one type on a mon of these types, in 1/4096ths. */
    int multiplier(int attack, int[] defence) {
        if (attack < 0 || attack >= chart.length) {
            return NEUTRAL;
        }
        long result = NEUTRAL;
        for (int type : defence) {
            if (type > TYPE_NONE && type < chart.length) {
                result = result * chart[attack][type] / NEUTRAL;
            }
        }
        return (int) result;
    }

    /**
     * The attack types that do other than neutral damage to a mon of these
     * types, strongest first, as {type, multiplier} pairs.
     */
    int[][] matchups(int[] defence) {
        int count = 0;
        int[][] all = new int[names.length][];
        for (int attack = 1; attack < names.length; attack++) {
            if (!isType(attack) || attack == TYPE_STELLAR) {
                continue;
            }
            int multiplier = multiplier(attack, defence);
            if (multiplier != NEUTRAL) {
                all[count++] = new int[] { attack, multiplier };
            }
        }
        int[][] result = new int[count][];
        System.arraycopy(all, 0, result, 0, count);
        java.util.Arrays.sort(result, (a, b) -> b[1] - a[1]);
        return result;
    }
}
