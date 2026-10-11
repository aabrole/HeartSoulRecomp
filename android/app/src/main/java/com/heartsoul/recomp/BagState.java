package com.heartsoul.recomp;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/** One parsed snapshot of the bag. Not changed after parsing. */
final class BagState {
    static final class Item {
        int id;
        int quantity;
        String name = "";
        String description = "";
        /** For a TM or HM: the move it teaches and that move's type. */
        String move = "";
        int moveType;
    }

    static final class Pocket {
        int id;
        String name = "";
        final List<Item> items = new ArrayList<>();
    }

    int frame = -1;
    final List<Pocket> pockets = new ArrayList<>();

    /** Null for text that cannot be parsed or that reports an error. */
    static BagState parse(String json) {
        if (json == null || json.isEmpty()) {
            return null;
        }
        try {
            JSONObject root = new JSONObject(json);
            if (root.has("error")) {
                return null; // keep showing the last bag that could be read
            }
            BagState bag = new BagState();
            bag.frame = root.optInt("frame", -1);
            JSONArray pockets = root.optJSONArray("pockets");
            for (int i = 0; pockets != null && i < pockets.length(); i++) {
                JSONObject pocketObject = pockets.getJSONObject(i);
                Pocket pocket = new Pocket();
                pocket.id = pocketObject.optInt("id", i);
                pocket.name = pocketObject.optString("name");
                JSONArray items = pocketObject.optJSONArray("items");
                for (int j = 0; items != null && j < items.length(); j++) {
                    JSONObject itemObject = items.getJSONObject(j);
                    Item item = new Item();
                    item.id = itemObject.optInt("id");
                    item.quantity = itemObject.optInt("qty");
                    item.name = itemObject.optString("name");
                    item.description = itemObject.optString("desc");
                    item.move = itemObject.optString("move");
                    item.moveType = itemObject.optInt("typeId");
                    pocket.items.add(item);
                }
                bag.pockets.add(pocket);
            }
            return bag;
        } catch (JSONException e) {
            return null;
        }
    }
}
