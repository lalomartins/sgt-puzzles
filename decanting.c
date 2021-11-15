/*
 * decanting.c: implementation of the 'water sort' puzzle game
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

enum {
    MAX_LAYERS = 8,
    MAX_COLOURS = 12,
    MAX_TUBES = 16,
    MAX_DESC = MAX_TUBES*(MAX_LAYERS+1)
};

enum {
    TILE_SIZE = 20,
    TUBE_BORDER = 2,
    TUBE_SPACING = 10,
    MARGIN_H = 20,
    MARGIN_V = 30,
    WRAP_TUBES = 5
};

enum {
    COL_0,
    COL_1,
    COL_2,
    COL_3,
    COL_4,
    COL_5,
    COL_6,
    COL_7,
    COL_8,
    COL_9,
    COL_10,
    COL_11,
    COL_12,
    COL_13,
    COL_14,
    COL_15,
    COL_BACKGROUND,
    COL_TUBE,
    COL_HIDDEN,
    NCOLOURS
};

static const char HEX[16] = "0123456789abcdef";

struct game_params {
    int ncolours;
    int ntubes;
    int nlayers;
    bool hiddenlayers;
};

struct game_state {
    bool solved;
    struct game_params p;
    signed char tubes[MAX_TUBES][MAX_LAYERS];
};

struct game_ui {
    int selected;
};

static void free_game(game_state *state)
{
    sfree(state);
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *default_params(void)
{
    game_params *params = snew(game_params);

    params->ncolours = 7;
    params->ntubes = 9;
    params->nlayers = 4;
    params->hiddenlayers = false;

    return params;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    ret = default_params();

    switch (i)
    {
    case 0:
        ret->ncolours = 4;
        ret->ntubes = 6;
        *name = dupstr("Easy");
        break;
    
    case 1:
        ret->ncolours = 7;
        ret->ntubes = 9;
        *name = dupstr("Default");
        break;
    
    case 2:
        ret->ncolours = 12;
        ret->ntubes = 14;
        ret->nlayers = 5;
        *name = dupstr("Hard");
        break;
    
    case 3:
        ret->ncolours = 12;
        ret->ntubes = 14;
        ret->nlayers = 4;
        *name = dupstr("Testing");
        break;
    
    default:
        free_params(ret);
        return false;
    }

    *params = ret;
    return true;
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    params->ncolours = atoi(string);
    params->ntubes = -1;
    params->nlayers = 4;
    params->hiddenlayers = false;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->nlayers = atoi(string);
        params->ntubes = params->ncolours;
        params->ncolours = -1;
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'w') {
        string++;
        if (params->ntubes == -1) {
            params->nlayers = params->ncolours;
        }
        params->ncolours = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'h') {
        string++;
        params->hiddenlayers = true;
    }
    if (params->ntubes == -1) params->ntubes = params->ncolours + 2;
    if (params->ncolours == -1) params->ncolours = params->ntubes - 2;
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    if (full || params->ncolours + 2 != params->ntubes)
        sprintf(data, "%dx%dw%d", params->ntubes, params->nlayers,
                params->ncolours);
    else if (params->nlayers != 4)
        sprintf(data, "%dw%d", params->nlayers, params->ncolours);
    else
        sprintf(data, "%d", params->ncolours);
    
    if (params->hiddenlayers)
        strncat(data, "h", 1);

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

    ret[0].name = "Colours";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->ncolours);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Tubes";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->ntubes);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Layers";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->nlayers);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "Hide layers below top";
    ret[3].type = C_BOOLEAN;
    ret[3].u.boolean.bval = params->hiddenlayers;

    ret[4].name = NULL;
    ret[4].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *params = snew(game_params);

    params->ncolours = atoi(cfg[0].u.string.sval);
    params->ntubes = atoi(cfg[1].u.string.sval);
    params->nlayers = atoi(cfg[2].u.string.sval);
    params->hiddenlayers = cfg[3].u.boolean.bval;

    return params;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->ncolours < 2) return "Colours must be at least 2";
    if (params->nlayers < 2) return "Layers must be at least 2";
    if (params->ntubes < 3) return "Tubes must be at least 3";
    if (params->ncolours > MAX_COLOURS) return "Too many colours";
    if (params->nlayers > MAX_LAYERS) return "Too many layers";
    if (params->ntubes > MAX_TUBES) return "Too many tubes";
    if (params->ntubes <= params->ncolours)
            return "There must be more tubes than colours";
    return NULL;
}

static char *game_desc(const game_state *state)
{
    char buf[MAX_DESC];
    int tube, layer, c = 0;
    for (tube = 0; tube < state->p.ntubes; tube++) {
        for (layer = state->p.nlayers -1; layer >= 0; layer--) {
            if (state->tubes[tube][layer] != -1)
                buf[c++] = HEX[state->tubes[tube][layer]];
        }
        buf[c++] = ',';
    }
    buf[c - 1] = 0;
    return dupstr(buf);
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int tube, layer;
    game_state *state = snew(game_state);
    char *ret;
    state->p = *params;

    /* FIXME special case for development */
    if (params->ncolours == 12 && params->nlayers == 4) {
        free_game(state);
        return dupstr(
            "11a8,9437,b672,2632,856a,aba5,067b,"
            "0370,8194,495b,8059,2314");
    }
    /* /FIXME end */

    for (tube = 0; tube < params->ncolours; tube++) {
        for (layer = 0; layer < params->nlayers; layer++) {
            state->tubes[tube][layer] = (char)tube;
        }
    }
    for (; tube < params->ntubes; tube++) {
        for (layer = 0; layer < params->nlayers; layer++) {
            state->tubes[tube][layer] = -1;
        }
    }

    ret = game_desc(state);
    free_game(state);
    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int tube, layer, c;
    char token, ftype;
    char amounts[MAX_COLOURS];

    tube = layer = c = 0;
    for (ftype = 0; ftype < params->ncolours; ftype++)
        amounts[ftype] = 0;

    while(c < MAX_DESC && (token = desc[c++])) {
        if (token == ',') {
            tube++;
            if (tube >= params->ntubes)
                return "Too many tubes";
            layer = 0;
        }
        else {
            if (layer >= params->nlayers)
                return "Too many layers in tube";

            if (token >= '0' && token <= '9') {
                ftype = token - '0';
                layer++;
            }
            else if (token >= 'a' && token <= 'f') {
                ftype = token - 'a' + 10;
                layer++;
            }
            else if (token >= 'A' && token <= 'F') {
                ftype = token - 'A' + 10;
                layer++;
            }
            else return "Invalid character";

            if (ftype > params->ncolours)
                return "Invalid color";
            if (++amounts[ftype] > params->nlayers)
                return "Too much fluid of the same type";
        }
    }

    for (ftype = 0; ftype < params->ncolours; ftype++)
        if (amounts[ftype] < params->nlayers)
            return "Not enough fluid of at least one type";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int tube, layer, c;
    char token;
    game_state *state = snew(game_state);

    state->solved = false;
    state->p = *params;

    tube = layer = c = 0;

    do {
        token = desc[c++];
        if (token == ',' || token == 0) {
            for (; layer < params->nlayers; layer++) {
                state->tubes[tube][layer] = -1;
            }
            tube++;
            layer = 0;
        }
        if (layer >= params->nlayers) continue;
        if (token >= '0' && token <= '9') {
            state->tubes[tube][layer++] = token - '0';
        }
        if (token >= 'a' && token <= 'f') {
            state->tubes[tube][layer++] = token - 'a' + 10;
        }
        if (token >= 'A' && token <= 'F') {
            state->tubes[tube][layer++] = token - 'A' + 10;
        }
    } while(token && c <= MAX_DESC);

    for (; tube < params->ntubes; tube++) {
        for (layer = 0; layer < params->nlayers; layer++) {
            state->tubes[tube][layer] = -1;
        }
    }

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int tube, layer;
    game_state *ret = snew(game_state);

    ret->solved = state->solved;
    ret->p = state->p;

    for (tube = 0; tube < ret->p.ntubes; tube++) {
        for (layer = 0; layer < ret->p.nlayers; layer++) {
            ret->tubes[tube][layer] = state->tubes[tube][layer];
        }
    }

    return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    return NULL;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    char buf[MAX_TUBES*MAX_LAYERS*2 + 1];
    int tube, layer, c = 0;
    for (layer = state->p.nlayers -1; layer >= 0; layer--) {
        for (tube = 0; tube < state->p.ntubes; tube++) {
            if (state->tubes[tube][layer] == -1)
                buf[c++] = '_';
            else
                buf[c++] = HEX[state->tubes[tube][layer]];
            buf[c++] = ' ';
        }
        buf[c - 1] = '\n';
    }
    buf[c] = 0;
    return dupstr(buf);
}

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->selected = -1;
    return ui;
}

static char *encode_ui(const game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

struct game_drawstate {
    int tilesize;
    bool started;
    int selected;
    bool solved;
    signed char tubes[MAX_TUBES][MAX_LAYERS];
};

static void check_solved(game_state *state)
{
    int tube, layer, color;
    if (state->solved) return;

    for (tube = 0; tube < state->p.ntubes; tube++) {
        if (state->tubes[tube][0] == -1) continue;
        color = state->tubes[tube][0];
        for (layer = 1; layer < state->p.nlayers; layer++) {
            if (state->tubes[tube][layer] != color) return;
        }
    }

    state->solved = true;
}

static int get_tube_at(const game_state *state, const game_drawstate *ds,
                        int x, int y)
{
    int tube = -1, tx, wrap = MAX_TUBES;
    
    if (x <= MARGIN_H || y <= MARGIN_V) return -1;

    tube = (x - MARGIN_H) / (ds->tilesize + TUBE_SPACING);
    tx = MARGIN_H + tube * (ds->tilesize + TUBE_SPACING);
    // click between tubes or to the right of the last one
    if (x - tx > ds->tilesize) return -1;

    if (y > MARGIN_V + ds->tilesize * (state->p.nlayers + 1)) {
        if (state->p.ntubes > WRAP_TUBES) {
            y -= MARGIN_V + ds->tilesize * (state->p.nlayers + 1);
            if (y > ds->tilesize * (state->p.nlayers + 1))
                return -1;
            wrap = (state->p.ntubes + 1) / 2;
            tube += wrap;
        } else return -1;
    }

    return tube;
}

static void get_tube_top(const game_state *state, int tube, int *color, int *number, int *empty)
{
    if (state->tubes[tube][0] == -1) {
        *color = -1;
        *number = 0;
        if (empty) *empty = state->p.nlayers;
    } else {
        int layer;
        for (layer = state->p.nlayers -1; layer >= 0; layer--) {
            if ((*color = state->tubes[tube][layer]) != -1) {
                if (empty) *empty = state->p.nlayers - layer - 1;
                *number = 1;
                while (--layer >= 0) {
                    if (*color == state->tubes[tube][layer]) (*number)++;
                    else break;
                }
                return;
            }
        }
    }
}

static int can_pour(const game_state *state, int tube_from, int tube_to)
{
    // easy cases: either one is empty
    if (state->tubes[tube_from][0] == -1) return 0;
    if (state->tubes[tube_to][0] == -1) {
        int color, number;
        get_tube_top(state, tube_from, &color, &number, 0);
        return number;
    }

    // destination is full
    if (state->tubes[tube_to][state->p.nlayers - 1] != -1) return 0;

    {
        int color_from, number_from, color_to, number_to, empty;
        get_tube_top(state, tube_from, &color_from, &number_from, 0);
        get_tube_top(state, tube_to, &color_to, &number_to, &empty);
        if (color_from == color_to) return min(number_from, empty);
    }

    return 0;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    if (state->solved) return NULL;

    if (button == LEFT_BUTTON) {
        int tube = get_tube_at(state, ds, x, y);
        if (tube == -1) return NULL;
        if (tube == ui->selected) ui->selected = -1;
        else {
            if (ui->selected != -1) {
                int pouring = can_pour(state, ui->selected, tube);
                if (pouring) {
                    char buf[10];
                    sprintf(buf, "p %d %d %d", ui->selected, tube, pouring);
                    ui->selected = -1;
                    return dupstr(buf);
                }
                // TODO flash?
                return NULL;
            }
            ui->selected = tube;
        }
        return UI_UPDATE;
    }

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int tube_from, tube_to, number;
    if (sscanf(move, "p %d %d %d", &tube_from, &tube_to, &number)) {
        game_state *new_state = dup_game(state);
        int layer = state->p.nlayers - 1, layer_end, color;
        while (layer > 0 && state->tubes[tube_from][layer] == -1)
            layer--;
        if (layer < number - 1) {
            free_game(new_state);
            return NULL;
        }
        layer_end = layer - number;
        color = state->tubes[tube_from][layer];
        while (layer > layer_end)
            new_state->tubes[tube_from][layer--] = -1;
        layer = state->p.nlayers - 1;
        while (layer >= 0 && state->tubes[tube_to][layer] == -1)
            layer--;
        if (state->p.nlayers - layer < number) {
            free_game(new_state);
            return NULL;
        }
        layer_end = ++layer + number;
        while (layer < layer_end)
            new_state->tubes[tube_to][layer++] = color;

        check_solved(new_state);
        return new_state;
    }
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    int tubes_x, tubes_y;
    /* Ideally we'd like a different wrap width for portrait
     * vs. landscape screens, but we don't have that info atm */
    if (params->ntubes > WRAP_TUBES) {
        tubes_x = (params->ntubes + 1) / 2;
        tubes_y = 2;
    } else {
        tubes_x = params->ntubes;
        tubes_y = 1;
    }

    *x = MARGIN_H * 2
        + tubes_x * tilesize
        + (tubes_x - 1) * TUBE_SPACING;
    *y = MARGIN_V * 2
        + tubes_y * tilesize * (params->nlayers + 1)
        + (tubes_y - 1) * TUBE_SPACING;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static const unsigned long palette_default[MAX_COLOURS] = {
    0x8ACB97,
    0x48B4EA,
    0xEB760C,
    0xEA6F8E,
    0xF6237E,
    0xFBE121,
    0xCD212A,
    0x9077B4,
    0x0064FF,
    0x7D318C,
    0x3E9B43,
    0x0000FF,
};

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int c;

    for (c = 0; c < MAX_COLOURS; c++) {
         ret[(COL_0 + c) * 3 + 0] = (float)
            ((palette_default[c] & 0xff0000) >> 16) / 256.0F;
         ret[(COL_0 + c) * 3 + 1] = (float)
            ((palette_default[c] & 0xff00) >> 8) / 256.0F;
         ret[(COL_0 + c) * 3 + 2] = (float)
            ((palette_default[c] & 0xff)) / 256.0F;
    }
    for (c = 0; c < 3; c++) {
        ret[COL_TUBE * 3 + c] = 0.03125F;
        ret[COL_HIDDEN * 3 + c] = 0.5F;
    }
    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int tube, layer;
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->selected = -1;
    ds->started = false;
    ds->solved = false;

    for (tube = 0; tube < state->p.ntubes; tube++) {
        for (layer = 0; layer < state->p.nlayers; layer++) {
            ds->tubes[tube][layer] = -2;
        }
    }

    return ds;
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int wrap = MAX_TUBES;
    int tube, layer;

    if (!ds->started) {
        int w, h;
        game_compute_size(&state->p, ds->tilesize, &w, &h);
        draw_rect(dr, 0, 0, w, h, COL_BACKGROUND);
        draw_update(dr, 0, 0, w, h);
    }
    if (state->solved != ds->solved) {
        int w, h;
        game_compute_size(&state->p, ds->tilesize, &w, &h);
        draw_rect(dr, 0, 0, w, h, state->solved ? COL_0 : COL_BACKGROUND);
        draw_update(dr, 0, 0, w, h);
        for (tube = 0; tube < state->p.ntubes; tube++) {
            for (layer = 0; layer < state->p.nlayers; layer++) {
                ds->tubes[tube][layer] = -2;
            }
        }
        ds->selected = -1;
        ds->solved = state->solved;
    }

    if (state->p.ntubes > WRAP_TUBES)
        wrap = (state->p.ntubes + 1) / 2;
    
    for (tube = 0; tube < state->p.ntubes; tube++) {
        bool tube_update = ds->selected != ui->selected
            && (ds->selected == tube || ui->selected == tube);
        bool did_update = false;
        int tx = MARGIN_H + tube * (ds->tilesize + TUBE_SPACING);
        int ty = MARGIN_V;
        int ty_base = ty;
        if (tube >= wrap) {
            ty += TUBE_SPACING + ds->tilesize * (state->p.nlayers + 1);
            ty_base = ty;
            tx = MARGIN_H + (tube - wrap) * (ds->tilesize + TUBE_SPACING);
        }
        if (ui->selected != tube) ty += ds->tilesize;

        if (tube_update) {
            draw_rect(dr, tx, ty_base, ds->tilesize, ds->tilesize * (state->p.nlayers + 1), COL_BACKGROUND);
        }

        for (layer = 0; layer < state->p.nlayers; layer++) {
            int color = state->tubes[tube][layer];
            if (tube_update || color != ds->tubes[tube][layer]) {
                ds->tubes[tube][layer] = color;
                int y = ty + ds->tilesize * (state->p.nlayers - layer - 1);
                if (color >= 0)
                    draw_rect(dr, tx, y, ds->tilesize, ds->tilesize,
                            COL_0 + color);
                else
                    draw_rect(dr, tx, y, ds->tilesize, ds->tilesize,
                            COL_BACKGROUND);
                did_update = true;
            }
        }

        if (did_update) {
            draw_rect_outline(dr, tx, ty, ds->tilesize, ds->tilesize * state->p.nlayers, COL_TUBE);
            // outline draws slightly out of its box on some frontends
            draw_update(dr, tx - 2, ty_base - 2, ds->tilesize + 4, ds->tilesize * (state->p.nlayers + 1) + 4);
        }
    }

    ds->selected = ui->selected;
    if (!ds->started) ds->started = true;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
}

static int game_status(const game_state *state)
{
    return state->solved ? 1 : 0;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
    return true;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
}

#ifdef COMBINED
#define thegame decanting
#endif

const struct game thegame = {
    "Decanting", NULL, NULL,
    default_params,
    game_fetch_preset, NULL,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    true, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    false, solve_game,
    true, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* game_request_keys */
    game_changed_state,
    interpret_move,
    execute_move,
    TILE_SIZE + TUBE_BORDER * 2, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    false, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
    false, game_timing_state,
    0,				       /* flags */
};
