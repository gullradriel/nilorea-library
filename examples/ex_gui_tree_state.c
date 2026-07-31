/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 *@example ex_gui_tree_state.c
 *@brief Tree view-state regression test (headless): unfolded branches, the
 *       selection and the scroll position survive a rebuild that replaces every
 *       node, which is what a model reloaded from disk does.
 *@author Castagnier Mickael
 *@version 1.0
 */

#define ALLEGRO_UNSTABLE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_gui.h"

static int failures = 0;

static void expect_true(const char* label, int cond) {
    if (!cond) {
        n_log(LOG_ERR, "%s: condition false", label);
        failures++;
    }
}

/* Two folders with two requests each, plus a nested sub-folder. Rebuilt from
   scratch each time, exactly like a collection reloaded after a save: every
   node index and user_data pointer is new. */
static void build_tree(N_GUI_TREE* tree, void** tags) {
    int api, users, admin;
    n_gui_tree_clear(tree);
    api = n_gui_tree_add_node(tree, "API", -1, tags[0]);
    n_gui_tree_add_node(tree, "GET /health", api, tags[1]);
    users = n_gui_tree_add_node(tree, "Users", api, tags[2]);
    n_gui_tree_add_node(tree, "GET /users", users, tags[3]);
    n_gui_tree_add_node(tree, "POST /users", users, tags[4]);
    admin = n_gui_tree_add_node(tree, "Admin", -1, tags[5]);
    n_gui_tree_add_node(tree, "GET /admin/stats", admin, tags[6]);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    set_log_level(LOG_ERR);

    if (!al_init() || !al_init_font_addon()) {
        n_log(LOG_ERR, "allegro init failed");
        return 1;
    }
    ALLEGRO_FONT* font = al_create_builtin_font();
    if (!font) {
        n_log(LOG_ERR, "al_create_builtin_font failed");
        return 1;
    }
    N_GUI_CTX* gui = n_gui_new_ctx(font);
    if (!gui) {
        n_log(LOG_ERR, "n_gui_new_ctx failed");
        al_destroy_font(font);
        return 1;
    }
    n_gui_set_display_size(gui, 400.0f, 300.0f);

    int win = n_gui_add_window(gui, "Panel", 0.0f, 0.0f, 300.0f, 200.0f);
    /* short on purpose: three rows visible, so the view really can scroll */
    N_GUI_TREE* tree = n_gui_tree_create(gui, win, 0.0f, 0.0f, 200.0f, 60.0f, NULL, NULL);
    expect_true("tree created", tree != NULL);
    if (!tree) return 1;

    /* two generations of "model objects": the second rebuild gets different
       pointers, so anything matching on user_data would fail */
    int gen1[7], gen2[7];
    void* tags1[7];
    void* tags2[7];
    for (int i = 0; i < 7; i++) {
        tags1[i] = &gen1[i];
        tags2[i] = &gen2[i];
    }

    build_tree(tree, tags1);
    expect_true("nodes added", tree->nb_nodes == 7);

    /* paths identify a node independently of its index */
    {
        char buf[N_GUI_TREE_PATH_MAX];
        expect_true("root path", n_gui_tree_node_path(tree, 0, buf, sizeof(buf)) == 0 &&
                                     strcmp(buf, "API") == 0);
        expect_true("nested path",
                    n_gui_tree_node_path(tree, 3, buf, sizeof(buf)) == 0 &&
                        strcmp(buf, "API\x1f" "Users\x1f" "GET /users") == 0);
        expect_true("find by path", n_gui_tree_find_by_path(tree, "API\x1f" "Users") == 2);
        expect_true("unknown path", n_gui_tree_find_by_path(tree, "Nope") == -1);
        expect_true("path of a bad index",
                    n_gui_tree_node_path(tree, 99, buf, sizeof(buf)) == -1);
    }

    /* unfold two branches, select a leaf inside them, scroll down a row */
    n_gui_tree_toggle_expand(tree, 0); /* API */
    n_gui_tree_toggle_expand(tree, 2); /* API/Users */
    expect_true("branches expanded", tree->nodes[0].expanded && tree->nodes[2].expanded);
    {
        int leaf = n_gui_tree_find_by_path(tree, "API\x1f" "Users\x1f" "POST /users");
        expect_true("leaf resolves", leaf >= 0);
        n_gui_tree_set_selection(tree, leaf);
        expect_true("leaf selected", n_gui_tree_get_selection(tree) == leaf);
    }
    n_gui_listbox_set_scroll_offset(gui, tree->listbox_id, 1);
    n_gui_listbox_set_h_scroll(gui, tree->listbox_id, 12.0f);

    /* the rebuild a save-and-reload triggers */
    {
        N_GUI_TREE_STATE st;
        expect_true("state saved", n_gui_tree_state_save(tree, &st) == 0);
        expect_true("both expanded branches captured", st.nb_expanded == 2);
        expect_true("selection captured",
                    st.selected && strcmp(st.selected, "API\x1f" "Users\x1f" "POST /users") == 0);

        build_tree(tree, tags2); /* every node is new */
        expect_true("rebuild collapsed everything",
                    !tree->nodes[0].expanded && !tree->nodes[2].expanded);
        expect_true("rebuild dropped the selection", n_gui_tree_get_selection(tree) == -1);

        n_gui_tree_state_apply(tree, &st);
        expect_true("branches unfolded again",
                    tree->nodes[0].expanded && tree->nodes[2].expanded);
        expect_true("selection restored on the new node",
                    n_gui_tree_get_selection(tree) ==
                        n_gui_tree_find_by_path(tree, "API\x1f" "Users\x1f" "POST /users"));
        expect_true("selection points at the new model object",
                    tree->nodes[n_gui_tree_get_selection(tree)].user_data == tags2[4]);
        expect_true("scroll restored",
                    n_gui_listbox_get_scroll_offset(gui, tree->listbox_id) == 1);
        expect_true("horizontal scroll restored",
                    n_gui_listbox_get_h_scroll(gui, tree->listbox_id) == 12.0f);

        /* a branch that disappeared is skipped, and one that appeared stays
           folded: applying a stale state must not resurrect or invent nodes */
        n_gui_tree_clear(tree);
        {
            int only = n_gui_tree_add_node(tree, "Admin", -1, tags2[5]);
            n_gui_tree_add_node(tree, "GET /admin/stats", only, tags2[6]);
        }
        n_gui_tree_state_apply(tree, &st);
        expect_true("missing branches are skipped", tree->nb_nodes == 2);
        expect_true("new branch stays folded", !tree->nodes[0].expanded);
        expect_true("missing selection leaves none selected",
                    n_gui_tree_get_selection(tree) == -1);

        n_gui_tree_state_free(&st);
        expect_true("freed state is empty", st.nb_expanded == 0 && st.selected == NULL);
        n_gui_tree_state_free(&st); /* idempotent */
    }

    /* a zeroed state is safe to free, and NULL arguments are no-ops */
    {
        N_GUI_TREE_STATE empty;
        memset(&empty, 0, sizeof(empty));
        n_gui_tree_state_free(&empty);
        n_gui_tree_state_apply(tree, NULL);
        n_gui_tree_state_apply(NULL, &empty);
        expect_true("NULL handling survived", tree->nb_nodes == 2);
    }

    n_gui_tree_free(&tree);
    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_tree_state: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_tree_state: all checks passed");
    return 0;
}
