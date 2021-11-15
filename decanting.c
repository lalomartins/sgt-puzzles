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
    MAX_TUBES = 16
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
    
    default:
        sfree(ret);
        return false;
    }

    *params = ret;
    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
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

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    return dupstr("FIXME");
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int tube, layer;
    game_state *state = snew(game_state);

    state->solved = false;
    state->p = *params;

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

static void free_game(game_state *state)
{
    sfree(state);
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
    return NULL;
}

static void free_ui(game_ui *ui)
{
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
    int FIXME;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    *x = *y = 10 * tilesize;	       /* FIXME */
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
    0xE30F66,
    0xFBE121,
    0xE4073A,
    0x9077B4,
    0x58C5BE,
    0x7D318C,
    0x3E9B43,
    0x2F7ABF,
};

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int c;

    for (c = 0; c < 8; c++) {
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
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->FIXME = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    /*
     * The initial contents of the window are not guaranteed and
     * can vary with front ends. To be on the safe side, all games
     * should start by drawing a big background-colour rectangle
     * covering the whole window.
     */
    draw_rect(dr, 0, 0, 10*ds->tilesize, 10*ds->tilesize, COL_BACKGROUND);
    draw_update(dr, 0, 0, 10*ds->tilesize, 10*ds->tilesize);
    status_bar(dr, "0 moves");
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
    return 0;
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
    20 /* FIXME */, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    false, false, game_print_size, game_print,
    true,			       /* wants_statusbar */
    false, game_timing_state,
    0,				       /* flags */
};
