/*
 * net.c: Net game.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"

const char *const game_name = "Net";

#define PI 3.141592653589793238462643383279502884197169399

#define MATMUL(xr,yr,m,x,y) do { \
    float rx, ry, xx = (x), yy = (y), *mat = (m); \
    rx = mat[0] * xx + mat[2] * yy; \
    ry = mat[1] * xx + mat[3] * yy; \
    (xr) = rx; (yr) = ry; \
} while (0)

/* Direction and other bitfields */
#define R 0x01
#define U 0x02
#define L 0x04
#define D 0x08
#define LOCKED 0x10
#define ACTIVE 0x20
/* Corner flags go in the barriers array */
#define RU 0x10
#define UL 0x20
#define LD 0x40
#define DR 0x80

/* Rotations: Anticlockwise, Clockwise, Flip, general rotate */
#define A(x) ( (((x) & 0x07) << 1) | (((x) & 0x08) >> 3) )
#define C(x) ( (((x) & 0x0E) >> 1) | (((x) & 0x01) << 3) )
#define F(x) ( (((x) & 0x0C) >> 2) | (((x) & 0x03) << 2) )
#define ROT(x, n) ( ((n)&3) == 0 ? (x) : \
		    ((n)&3) == 1 ? A(x) : \
		    ((n)&3) == 2 ? F(x) : C(x) )

/* X and Y displacements */
#define X(x) ( (x) == R ? +1 : (x) == L ? -1 : 0 )
#define Y(x) ( (x) == D ? +1 : (x) == U ? -1 : 0 )

/* Bit count */
#define COUNT(x) ( (((x) & 0x08) >> 3) + (((x) & 0x04) >> 2) + \
		   (((x) & 0x02) >> 1) + ((x) & 0x01) )

#define TILE_SIZE 32
#define TILE_BORDER 1
#define WINDOW_OFFSET 16

#define ROTATE_TIME 0.1F
#define FLASH_FRAME 0.05F

enum {
    COL_BACKGROUND,
    COL_LOCKED,
    COL_BORDER,
    COL_WIRE,
    COL_ENDPOINT,
    COL_POWERED,
    COL_BARRIER,
    NCOLOURS
};

struct game_params {
    int width;
    int height;
    int wrapping;
    float barrier_probability;
};

struct game_state {
    int width, height, cx, cy, wrapping, completed, last_rotate_dir;
    unsigned char *tiles;
    unsigned char *barriers;
};

#define OFFSET(x2,y2,x1,y1,dir,state) \
    ( (x2) = ((x1) + (state)->width + X((dir))) % (state)->width, \
      (y2) = ((y1) + (state)->height + Y((dir))) % (state)->height)

#define index(state, a, x, y) ( a[(y) * (state)->width + (x)] )
#define tile(state, x, y)     index(state, (state)->tiles, x, y)
#define barrier(state, x, y)  index(state, (state)->barriers, x, y)

struct xyd {
    int x, y, direction;
};

static int xyd_cmp(void *av, void *bv) {
    struct xyd *a = (struct xyd *)av;
    struct xyd *b = (struct xyd *)bv;
    if (a->x < b->x)
	return -1;
    if (a->x > b->x)
	return +1;
    if (a->y < b->y)
	return -1;
    if (a->y > b->y)
	return +1;
    if (a->direction < b->direction)
	return -1;
    if (a->direction > b->direction)
	return +1;
    return 0;
};

static struct xyd *new_xyd(int x, int y, int direction)
{
    struct xyd *xyd = snew(struct xyd);
    xyd->x = x;
    xyd->y = y;
    xyd->direction = direction;
    return xyd;
}

/* ----------------------------------------------------------------------
 * Manage game parameters.
 */
game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->width = 5;
    ret->height = 5;
    ret->wrapping = FALSE;
    ret->barrier_probability = 0.0;

    return ret;
}

int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];
    static const struct { int x, y, wrap; } values[] = {
        {5, 5, FALSE},
        {7, 7, FALSE},
        {9, 9, FALSE},
        {11, 11, FALSE},
        {13, 11, FALSE},
        {5, 5, TRUE},
        {7, 7, TRUE},
        {9, 9, TRUE},
        {11, 11, TRUE},
        {13, 11, TRUE},
    };

    if (i < 0 || i >= lenof(values))
        return FALSE;

    ret = snew(game_params);
    ret->width = values[i].x;
    ret->height = values[i].y;
    ret->wrapping = values[i].wrap;
    ret->barrier_probability = 0.0;

    sprintf(str, "%dx%d%s", ret->width, ret->height,
            ret->wrapping ? " wrapping" : "");

    *name = dupstr(str);
    *params = ret;
    return TRUE;
}

void free_params(game_params *params)
{
    sfree(params);
}

game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

/* ----------------------------------------------------------------------
 * Randomly select a new game seed.
 */

char *new_game_seed(game_params *params)
{
    /*
     * The full description of a Net game is far too large to
     * encode directly in the seed, so by default we'll have to go
     * for the simple approach of providing a random-number seed.
     * 
     * (This does not restrict me from _later on_ inventing a seed
     * string syntax which can never be generated by this code -
     * for example, strings beginning with a letter - allowing me
     * to type in a precise game, and have new_game detect it and
     * understand it and do something completely different.)
     */
    char buf[40];
    sprintf(buf, "%d", rand());
    return dupstr(buf);
}

/* ----------------------------------------------------------------------
 * Construct an initial game state, given a seed and parameters.
 */

game_state *new_game(game_params *params, char *seed)
{
    random_state *rs;
    game_state *state;
    tree234 *possibilities, *barriers;
    int w, h, x, y, nbarriers;

    assert(params->width > 0 && params->height > 0);
    assert(params->width > 1 || params->height > 1);

    /*
     * Create a blank game state.
     */
    state = snew(game_state);
    w = state->width = params->width;
    h = state->height = params->height;
    state->cx = state->width / 2;
    state->cy = state->height / 2;
    state->wrapping = params->wrapping;
    state->last_rotate_dir = +1;       /* *shrug* */
    state->completed = FALSE;
    state->tiles = snewn(state->width * state->height, unsigned char);
    memset(state->tiles, 0, state->width * state->height);
    state->barriers = snewn(state->width * state->height, unsigned char);
    memset(state->barriers, 0, state->width * state->height);

    /*
     * Set up border barriers if this is a non-wrapping game.
     */
    if (!state->wrapping) {
	for (x = 0; x < state->width; x++) {
	    barrier(state, x, 0) |= U;
	    barrier(state, x, state->height-1) |= D;
	}
	for (y = 0; y < state->height; y++) {
	    barrier(state, 0, y) |= L;
	    barrier(state, state->width-1, y) |= R;
	}
    }

    /*
     * Seed the internal random number generator.
     */
    rs = random_init(seed, strlen(seed));

    /*
     * Construct the unshuffled grid.
     * 
     * To do this, we simply start at the centre point, repeatedly
     * choose a random possibility out of the available ways to
     * extend a used square into an unused one, and do it. After
     * extending the third line out of a square, we remove the
     * fourth from the possibilities list to avoid any full-cross
     * squares (which would make the game too easy because they
     * only have one orientation).
     * 
     * The slightly worrying thing is the avoidance of full-cross
     * squares. Can this cause our unsophisticated construction
     * algorithm to paint itself into a corner, by getting into a
     * situation where there are some unreached squares and the
     * only way to reach any of them is to extend a T-piece into a
     * full cross?
     * 
     * Answer: no it can't, and here's a proof.
     * 
     * Any contiguous group of such unreachable squares must be
     * surrounded on _all_ sides by T-pieces pointing away from the
     * group. (If not, then there is a square which can be extended
     * into one of the `unreachable' ones, and so it wasn't
     * unreachable after all.) In particular, this implies that
     * each contiguous group of unreachable squares must be
     * rectangular in shape (any deviation from that yields a
     * non-T-piece next to an `unreachable' square).
     * 
     * So we have a rectangle of unreachable squares, with T-pieces
     * forming a solid border around the rectangle. The corners of
     * that border must be connected (since every tile connects all
     * the lines arriving in it), and therefore the border must
     * form a closed loop around the rectangle.
     * 
     * But this can't have happened in the first place, since we
     * _know_ we've avoided creating closed loops! Hence, no such
     * situation can ever arise, and the naive grid construction
     * algorithm will guaranteeably result in a complete grid
     * containing no unreached squares, no full crosses _and_ no
     * closed loops. []
     */
    possibilities = newtree234(xyd_cmp);

    if (state->cx+1 < state->width)
	add234(possibilities, new_xyd(state->cx, state->cy, R));
    if (state->cy-1 >= 0)
	add234(possibilities, new_xyd(state->cx, state->cy, U));
    if (state->cx-1 >= 0)
	add234(possibilities, new_xyd(state->cx, state->cy, L));
    if (state->cy+1 < state->height)
	add234(possibilities, new_xyd(state->cx, state->cy, D));

    while (count234(possibilities) > 0) {
	int i;
	struct xyd *xyd;
	int x1, y1, d1, x2, y2, d2, d;

	/*
	 * Extract a randomly chosen possibility from the list.
	 */
	i = random_upto(rs, count234(possibilities));
	xyd = delpos234(possibilities, i);
	x1 = xyd->x;
	y1 = xyd->y;
	d1 = xyd->direction;
	sfree(xyd);

	OFFSET(x2, y2, x1, y1, d1, state);
	d2 = F(d1);
#ifdef DEBUG
	printf("picked (%d,%d,%c) <-> (%d,%d,%c)\n",
	       x1, y1, "0RU3L567D9abcdef"[d1], x2, y2, "0RU3L567D9abcdef"[d2]);
#endif

	/*
	 * Make the connection. (We should be moving to an as yet
	 * unused tile.)
	 */
	tile(state, x1, y1) |= d1;
	assert(tile(state, x2, y2) == 0);
	tile(state, x2, y2) |= d2;

	/*
	 * If we have created a T-piece, remove its last
	 * possibility.
	 */
	if (COUNT(tile(state, x1, y1)) == 3) {
	    struct xyd xyd1, *xydp;

	    xyd1.x = x1;
	    xyd1.y = y1;
	    xyd1.direction = 0x0F ^ tile(state, x1, y1);

	    xydp = find234(possibilities, &xyd1, NULL);

	    if (xydp) {
#ifdef DEBUG
		printf("T-piece; removing (%d,%d,%c)\n",
		       xydp->x, xydp->y, "0RU3L567D9abcdef"[xydp->direction]);
#endif
		del234(possibilities, xydp);
		sfree(xydp);
	    }
	}

	/*
	 * Remove all other possibilities that were pointing at the
	 * tile we've just moved into.
	 */
	for (d = 1; d < 0x10; d <<= 1) {
	    int x3, y3, d3;
	    struct xyd xyd1, *xydp;

	    OFFSET(x3, y3, x2, y2, d, state);
	    d3 = F(d);

	    xyd1.x = x3;
	    xyd1.y = y3;
	    xyd1.direction = d3;

	    xydp = find234(possibilities, &xyd1, NULL);

	    if (xydp) {
#ifdef DEBUG
		printf("Loop avoidance; removing (%d,%d,%c)\n",
		       xydp->x, xydp->y, "0RU3L567D9abcdef"[xydp->direction]);
#endif
		del234(possibilities, xydp);
		sfree(xydp);
	    }
	}

	/*
	 * Add new possibilities to the list for moving _out_ of
	 * the tile we have just moved into.
	 */
	for (d = 1; d < 0x10; d <<= 1) {
	    int x3, y3;

	    if (d == d2)
		continue;	       /* we've got this one already */

	    if (!state->wrapping) {
		if (d == U && y2 == 0)
		    continue;
		if (d == D && y2 == state->height-1)
		    continue;
		if (d == L && x2 == 0)
		    continue;
		if (d == R && x2 == state->width-1)
		    continue;
	    }

	    OFFSET(x3, y3, x2, y2, d, state);

	    if (tile(state, x3, y3))
		continue;	       /* this would create a loop */

#ifdef DEBUG
	    printf("New frontier; adding (%d,%d,%c)\n",
		   x2, y2, "0RU3L567D9abcdef"[d]);
#endif
	    add234(possibilities, new_xyd(x2, y2, d));
	}
    }
    /* Having done that, we should have no possibilities remaining. */
    assert(count234(possibilities) == 0);
    freetree234(possibilities);

    /*
     * Now compute a list of the possible barrier locations.
     */
    barriers = newtree234(xyd_cmp);
    for (y = 0; y < state->height; y++) {
	for (x = 0; x < state->width; x++) {

	    if (!(tile(state, x, y) & R) &&
                (state->wrapping || x < state->width-1))
		add234(barriers, new_xyd(x, y, R));
	    if (!(tile(state, x, y) & D) &&
                (state->wrapping || y < state->height-1))
		add234(barriers, new_xyd(x, y, D));
	}
    }

    /*
     * Now shuffle the grid.
     */
    for (y = 0; y < state->height; y++) {
	for (x = 0; x < state->width; x++) {
	    int orig = tile(state, x, y);
	    int rot = random_upto(rs, 4);
	    tile(state, x, y) = ROT(orig, rot);
	}
    }

    /*
     * And now choose barrier locations. (We carefully do this
     * _after_ shuffling, so that changing the barrier rate in the
     * params while keeping the game seed the same will give the
     * same shuffled grid and _only_ change the barrier locations.
     * Also the way we choose barrier locations, by repeatedly
     * choosing one possibility from the list until we have enough,
     * is designed to ensure that raising the barrier rate while
     * keeping the seed the same will provide a superset of the
     * previous barrier set - i.e. if you ask for 10 barriers, and
     * then decide that's still too hard and ask for 20, you'll get
     * the original 10 plus 10 more, rather than getting 20 new
     * ones and the chance of remembering your first 10.)
     */
    nbarriers = (int)(params->barrier_probability * count234(barriers));
    assert(nbarriers >= 0 && nbarriers <= count234(barriers));

    while (nbarriers > 0) {
	int i;
	struct xyd *xyd;
	int x1, y1, d1, x2, y2, d2;

	/*
	 * Extract a randomly chosen barrier from the list.
	 */
	i = random_upto(rs, count234(barriers));
	xyd = delpos234(barriers, i);

	assert(xyd != NULL);

	x1 = xyd->x;
	y1 = xyd->y;
	d1 = xyd->direction;
	sfree(xyd);

	OFFSET(x2, y2, x1, y1, d1, state);
	d2 = F(d1);

	barrier(state, x1, y1) |= d1;
	barrier(state, x2, y2) |= d2;

	nbarriers--;
    }

    /*
     * Clean up the rest of the barrier list.
     */
    {
	struct xyd *xyd;

	while ( (xyd = delpos234(barriers, 0)) != NULL)
	    sfree(xyd);

	freetree234(barriers);
    }

    /*
     * Set up the barrier corner flags, for drawing barriers
     * prettily when they meet.
     */
    for (y = 0; y < state->height; y++) {
	for (x = 0; x < state->width; x++) {
            int dir;

            for (dir = 1; dir < 0x10; dir <<= 1) {
                int dir2 = A(dir);
                int x1, y1, x2, y2, x3, y3;
                int corner = FALSE;

                if (!(barrier(state, x, y) & dir))
                    continue;

                if (barrier(state, x, y) & dir2)
                    corner = TRUE;

                x1 = x + X(dir), y1 = y + Y(dir);
                if (x1 >= 0 && x1 < state->width &&
                    y1 >= 0 && y1 < state->height &&
                    (barrier(state, x1, y1) & dir2))
                    corner = TRUE;

                x2 = x + X(dir2), y2 = y + Y(dir2);
                if (x2 >= 0 && x2 < state->width &&
                    y2 >= 0 && y2 < state->height &&
                    (barrier(state, x2, y2) & dir))
                    corner = TRUE;

                if (corner) {
                    barrier(state, x, y) |= (dir << 4);
                    if (x1 >= 0 && x1 < state->width &&
                        y1 >= 0 && y1 < state->height)
                        barrier(state, x1, y1) |= (A(dir) << 4);
                    if (x2 >= 0 && x2 < state->width &&
                        y2 >= 0 && y2 < state->height)
                        barrier(state, x2, y2) |= (C(dir) << 4);
                    x3 = x + X(dir) + X(dir2), y3 = y + Y(dir) + Y(dir2);
                    if (x3 >= 0 && x3 < state->width &&
                        y3 >= 0 && y3 < state->height)
                        barrier(state, x3, y3) |= (F(dir) << 4);
                }
            }
	}
    }

    random_free(rs);

    return state;
}

game_state *dup_game(game_state *state)
{
    game_state *ret;

    ret = snew(game_state);
    ret->width = state->width;
    ret->height = state->height;
    ret->cx = state->cx;
    ret->cy = state->cy;
    ret->wrapping = state->wrapping;
    ret->completed = state->completed;
    ret->last_rotate_dir = state->last_rotate_dir;
    ret->tiles = snewn(state->width * state->height, unsigned char);
    memcpy(ret->tiles, state->tiles, state->width * state->height);
    ret->barriers = snewn(state->width * state->height, unsigned char);
    memcpy(ret->barriers, state->barriers, state->width * state->height);

    return ret;
}

void free_game(game_state *state)
{
    sfree(state->tiles);
    sfree(state->barriers);
    sfree(state);
}

/* ----------------------------------------------------------------------
 * Utility routine.
 */

/*
 * Compute which squares are reachable from the centre square, as a
 * quick visual aid to determining how close the game is to
 * completion. This is also a simple way to tell if the game _is_
 * completed - just call this function and see whether every square
 * is marked active.
 */
static unsigned char *compute_active(game_state *state)
{
    unsigned char *active;
    tree234 *todo;
    struct xyd *xyd;

    active = snewn(state->width * state->height, unsigned char);
    memset(active, 0, state->width * state->height);

    /*
     * We only store (x,y) pairs in todo, but it's easier to reuse
     * xyd_cmp and just store direction 0 every time.
     */
    todo = newtree234(xyd_cmp);
    index(state, active, state->cx, state->cy) = ACTIVE;
    add234(todo, new_xyd(state->cx, state->cy, 0));

    while ( (xyd = delpos234(todo, 0)) != NULL) {
	int x1, y1, d1, x2, y2, d2;

	x1 = xyd->x;
	y1 = xyd->y;
	sfree(xyd);

	for (d1 = 1; d1 < 0x10; d1 <<= 1) {
	    OFFSET(x2, y2, x1, y1, d1, state);
	    d2 = F(d1);

	    /*
	     * If the next tile in this direction is connected to
	     * us, and there isn't a barrier in the way, and it
	     * isn't already marked active, then mark it active and
	     * add it to the to-examine list.
	     */
	    if ((tile(state, x1, y1) & d1) &&
		(tile(state, x2, y2) & d2) &&
		!(barrier(state, x1, y1) & d1) &&
		!index(state, active, x2, y2)) {
		index(state, active, x2, y2) = ACTIVE;
		add234(todo, new_xyd(x2, y2, 0));
	    }
	}
    }
    /* Now we expect the todo list to have shrunk to zero size. */
    assert(count234(todo) == 0);
    freetree234(todo);

    return active;
}

/* ----------------------------------------------------------------------
 * Process a move.
 */
game_state *make_move(game_state *state, int x, int y, int button)
{
    game_state *ret;
    int tx, ty, orig;

    /*
     * All moves in Net are made with the mouse.
     */
    if (button != LEFT_BUTTON &&
	button != MIDDLE_BUTTON &&
	button != RIGHT_BUTTON)
	return NULL;

    /*
     * The button must have been clicked on a valid tile.
     */
    x -= WINDOW_OFFSET + TILE_BORDER;
    y -= WINDOW_OFFSET + TILE_BORDER;
    if (x < 0 || y < 0)
	return NULL;
    tx = x / TILE_SIZE;
    ty = y / TILE_SIZE;
    if (tx >= state->width || ty >= state->height)
	return NULL;
    if (tx % TILE_SIZE >= TILE_SIZE - TILE_BORDER ||
	ty % TILE_SIZE >= TILE_SIZE - TILE_BORDER)
	return NULL;

    /*
     * The middle button locks or unlocks a tile. (A locked tile
     * cannot be turned, and is visually marked as being locked.
     * This is a convenience for the player, so that once they are
     * sure which way round a tile goes, they can lock it and thus
     * avoid forgetting later on that they'd already done that one;
     * and the locking also prevents them turning the tile by
     * accident. If they change their mind, another middle click
     * unlocks it.)
     */
    if (button == MIDDLE_BUTTON) {
	ret = dup_game(state);
	tile(ret, tx, ty) ^= LOCKED;
	return ret;
    }

    /*
     * The left and right buttons have no effect if clicked on a
     * locked tile.
     */
    if (tile(state, tx, ty) & LOCKED)
	return NULL;

    /*
     * Otherwise, turn the tile one way or the other. Left button
     * turns anticlockwise; right button turns clockwise.
     */
    ret = dup_game(state);
    orig = tile(ret, tx, ty);
    if (button == LEFT_BUTTON) {
	tile(ret, tx, ty) = A(orig);
        ret->last_rotate_dir = +1;
    } else {
	tile(ret, tx, ty) = C(orig);
        ret->last_rotate_dir = -1;
    }

    /*
     * Check whether the game has been completed.
     */
    {
	unsigned char *active = compute_active(ret);
	int x1, y1;
	int complete = TRUE;

	for (x1 = 0; x1 < ret->width; x1++)
	    for (y1 = 0; y1 < ret->height; y1++)
		if (!index(ret, active, x1, y1)) {
		    complete = FALSE;
		    goto break_label;  /* break out of two loops at once */
		}
	break_label:

	sfree(active);

	if (complete)
	    ret->completed = TRUE;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Routines for drawing the game position on the screen.
 */

struct game_drawstate {
    int started;
    int width, height;
    unsigned char *visible;
};

game_drawstate *game_new_drawstate(game_state *state)
{
    game_drawstate *ds = snew(game_drawstate);

    ds->started = FALSE;
    ds->width = state->width;
    ds->height = state->height;
    ds->visible = snewn(state->width * state->height, unsigned char);
    memset(ds->visible, 0xFF, state->width * state->height);

    return ds;
}

void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->visible);
    sfree(ds);
}

void game_size(game_params *params, int *x, int *y)
{
    *x = WINDOW_OFFSET * 2 + TILE_SIZE * params->width + TILE_BORDER;
    *y = WINDOW_OFFSET * 2 + TILE_SIZE * params->height + TILE_BORDER;
}

float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret;

    ret = snewn(NCOLOURS * 3, float);
    *ncolours = NCOLOURS;

    /*
     * Basic background colour is whatever the front end thinks is
     * a sensible default.
     */
    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    /*
     * Wires are black.
     */
    ret[COL_WIRE * 3 + 0] = 0.0F;
    ret[COL_WIRE * 3 + 1] = 0.0F;
    ret[COL_WIRE * 3 + 2] = 0.0F;

    /*
     * Powered wires and powered endpoints are cyan.
     */
    ret[COL_POWERED * 3 + 0] = 0.0F;
    ret[COL_POWERED * 3 + 1] = 1.0F;
    ret[COL_POWERED * 3 + 2] = 1.0F;

    /*
     * Barriers are red.
     */
    ret[COL_BARRIER * 3 + 0] = 1.0F;
    ret[COL_BARRIER * 3 + 1] = 0.0F;
    ret[COL_BARRIER * 3 + 2] = 0.0F;

    /*
     * Unpowered endpoints are blue.
     */
    ret[COL_ENDPOINT * 3 + 0] = 0.0F;
    ret[COL_ENDPOINT * 3 + 1] = 0.0F;
    ret[COL_ENDPOINT * 3 + 2] = 1.0F;

    /*
     * Tile borders are a darker grey than the background.
     */
    ret[COL_BORDER * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_BORDER * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_BORDER * 3 + 2] = 0.5F * ret[COL_BACKGROUND * 3 + 2];

    /*
     * Locked tiles are a grey in between those two.
     */
    ret[COL_LOCKED * 3 + 0] = 0.75F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_LOCKED * 3 + 1] = 0.75F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_LOCKED * 3 + 2] = 0.75F * ret[COL_BACKGROUND * 3 + 2];

    return ret;
}

static void draw_thick_line(frontend *fe, int x1, int y1, int x2, int y2,
                            int colour)
{
    draw_line(fe, x1-1, y1, x2-1, y2, COL_WIRE);
    draw_line(fe, x1+1, y1, x2+1, y2, COL_WIRE);
    draw_line(fe, x1, y1-1, x2, y2-1, COL_WIRE);
    draw_line(fe, x1, y1+1, x2, y2+1, COL_WIRE);
    draw_line(fe, x1, y1, x2, y2, colour);
}

static void draw_rect_coords(frontend *fe, int x1, int y1, int x2, int y2,
                             int colour)
{
    int mx = (x1 < x2 ? x1 : x2);
    int my = (y1 < y2 ? y1 : y2);
    int dx = (x2 + x1 - 2*mx + 1);
    int dy = (y2 + y1 - 2*my + 1);

    draw_rect(fe, mx, my, dx, dy, colour);
}

static void draw_barrier_corner(frontend *fe, int x, int y, int dir, int phase)
{
    int bx = WINDOW_OFFSET + TILE_SIZE * x;
    int by = WINDOW_OFFSET + TILE_SIZE * y;
    int x1, y1, dx, dy, dir2;

    dir >>= 4;

    dir2 = A(dir);
    dx = X(dir) + X(dir2);
    dy = Y(dir) + Y(dir2);
    x1 = (dx > 0 ? TILE_SIZE+TILE_BORDER-1 : 0);
    y1 = (dy > 0 ? TILE_SIZE+TILE_BORDER-1 : 0);

    if (phase == 0) {
        draw_rect_coords(fe, bx+x1, by+y1,
                         bx+x1-TILE_BORDER*dx, by+y1-(TILE_BORDER-1)*dy,
                         COL_WIRE);
        draw_rect_coords(fe, bx+x1, by+y1,
                         bx+x1-(TILE_BORDER-1)*dx, by+y1-TILE_BORDER*dy,
                         COL_WIRE);
    } else {
        draw_rect_coords(fe, bx+x1, by+y1,
                         bx+x1-(TILE_BORDER-1)*dx, by+y1-(TILE_BORDER-1)*dy,
                         COL_BARRIER);
    }
}

static void draw_barrier(frontend *fe, int x, int y, int dir, int phase)
{
    int bx = WINDOW_OFFSET + TILE_SIZE * x;
    int by = WINDOW_OFFSET + TILE_SIZE * y;
    int x1, y1, w, h;

    x1 = (X(dir) > 0 ? TILE_SIZE : X(dir) == 0 ? TILE_BORDER : 0);
    y1 = (Y(dir) > 0 ? TILE_SIZE : Y(dir) == 0 ? TILE_BORDER : 0);
    w = (X(dir) ? TILE_BORDER : TILE_SIZE - TILE_BORDER);
    h = (Y(dir) ? TILE_BORDER : TILE_SIZE - TILE_BORDER);

    if (phase == 0) {
        draw_rect(fe, bx+x1-X(dir), by+y1-Y(dir), w, h, COL_WIRE);
    } else {
        draw_rect(fe, bx+x1, by+y1, w, h, COL_BARRIER);
    }
}

static void draw_tile(frontend *fe, game_state *state, int x, int y, int tile,
                      float angle)
{
    int bx = WINDOW_OFFSET + TILE_SIZE * x;
    int by = WINDOW_OFFSET + TILE_SIZE * y;
    float matrix[4];
    float cx, cy, ex, ey, tx, ty;
    int dir, col, phase;

    /*
     * When we draw a single tile, we must draw everything up to
     * and including the borders around the tile. This means that
     * if the neighbouring tiles have connections to those borders,
     * we must draw those connections on the borders themselves.
     *
     * This would be terribly fiddly if we ever had to draw a tile
     * while its neighbour was in mid-rotate, because we'd have to
     * arrange to _know_ that the neighbour was being rotated and
     * hence had an anomalous effect on the redraw of this tile.
     * Fortunately, the drawing algorithm avoids ever calling us in
     * this circumstance: we're either drawing lots of straight
     * tiles at game start or after a move is complete, or we're
     * repeatedly drawing only the rotating tile. So no problem.
     */

    /*
     * So. First blank the tile out completely: draw a big
     * rectangle in border colour, and a smaller rectangle in
     * background colour to fill it in.
     */
    draw_rect(fe, bx, by, TILE_SIZE+TILE_BORDER, TILE_SIZE+TILE_BORDER,
              COL_BORDER);
    draw_rect(fe, bx+TILE_BORDER, by+TILE_BORDER,
              TILE_SIZE-TILE_BORDER, TILE_SIZE-TILE_BORDER,
              tile & LOCKED ? COL_LOCKED : COL_BACKGROUND);

    /*
     * Set up the rotation matrix.
     */
    matrix[0] = (float)cos(angle * PI / 180.0);
    matrix[1] = (float)-sin(angle * PI / 180.0);
    matrix[2] = (float)sin(angle * PI / 180.0);
    matrix[3] = (float)cos(angle * PI / 180.0);

    /*
     * Draw the wires.
     */
    cx = cy = TILE_BORDER + (TILE_SIZE-TILE_BORDER) / 2.0F - 0.5F;
    col = (tile & ACTIVE ? COL_POWERED : COL_WIRE);
    for (dir = 1; dir < 0x10; dir <<= 1) {
        if (tile & dir) {
            ex = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * X(dir);
            ey = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * Y(dir);
            MATMUL(tx, ty, matrix, ex, ey);
            draw_thick_line(fe, bx+(int)cx, by+(int)cy,
			    bx+(int)(cx+tx), by+(int)(cy+ty),
                            COL_WIRE);
        }
    }
    for (dir = 1; dir < 0x10; dir <<= 1) {
        if (tile & dir) {
            ex = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * X(dir);
            ey = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * Y(dir);
            MATMUL(tx, ty, matrix, ex, ey);
            draw_line(fe, bx+(int)cx, by+(int)cy,
		      bx+(int)(cx+tx), by+(int)(cy+ty), col);
        }
    }

    /*
     * Draw the box in the middle. We do this in blue if the tile
     * is an unpowered endpoint, in cyan if the tile is a powered
     * endpoint, in black if the tile is the centrepiece, and
     * otherwise not at all.
     */
    col = -1;
    if (x == state->cx && y == state->cy)
        col = COL_WIRE;
    else if (COUNT(tile) == 1) {
        col = (tile & ACTIVE ? COL_POWERED : COL_ENDPOINT);
    }
    if (col >= 0) {
        int i, points[8];

        points[0] = +1; points[1] = +1;
        points[2] = +1; points[3] = -1;
        points[4] = -1; points[5] = -1;
        points[6] = -1; points[7] = +1;

        for (i = 0; i < 8; i += 2) {
            ex = (TILE_SIZE * 0.24F) * points[i];
            ey = (TILE_SIZE * 0.24F) * points[i+1];
            MATMUL(tx, ty, matrix, ex, ey);
            points[i] = bx+(int)(cx+tx);
            points[i+1] = by+(int)(cy+ty);
        }

        draw_polygon(fe, points, 4, TRUE, col);
        draw_polygon(fe, points, 4, FALSE, COL_WIRE);
    }

    /*
     * Draw the points on the border if other tiles are connected
     * to us.
     */
    for (dir = 1; dir < 0x10; dir <<= 1) {
        int dx, dy, px, py, lx, ly, vx, vy, ox, oy;

        dx = X(dir);
        dy = Y(dir);

        ox = x + dx;
        oy = y + dy;

        if (ox < 0 || ox >= state->width || oy < 0 || oy >= state->height)
            continue;

        if (!(tile(state, ox, oy) & F(dir)))
            continue;

        px = bx + (int)(dx>0 ? TILE_SIZE + TILE_BORDER - 1 : dx<0 ? 0 : cx);
        py = by + (int)(dy>0 ? TILE_SIZE + TILE_BORDER - 1 : dy<0 ? 0 : cy);
        lx = dx * (TILE_BORDER-1);
        ly = dy * (TILE_BORDER-1);
        vx = (dy ? 1 : 0);
        vy = (dx ? 1 : 0);

        if (angle == 0.0 && (tile & dir)) {
            /*
             * If we are fully connected to the other tile, we must
             * draw right across the tile border. (We can use our
             * own ACTIVE state to determine what colour to do this
             * in: if we are fully connected to the other tile then
             * the two ACTIVE states will be the same.)
             */
            draw_rect_coords(fe, px-vx, py-vy, px+lx+vx, py+ly+vy, COL_WIRE);
            draw_rect_coords(fe, px, py, px+lx, py+ly,
                             (tile & ACTIVE) ? COL_POWERED : COL_WIRE);
        } else {
            /*
             * The other tile extends into our border, but isn't
             * actually connected to us. Just draw a single black
             * dot.
             */
            draw_rect_coords(fe, px, py, px, py, COL_WIRE);
        }
    }

    /*
     * Draw barrier corners, and then barriers.
     */
    for (phase = 0; phase < 2; phase++) {
        for (dir = 1; dir < 0x10; dir <<= 1)
            if (barrier(state, x, y) & (dir << 4))
                draw_barrier_corner(fe, x, y, dir << 4, phase);
        for (dir = 1; dir < 0x10; dir <<= 1)
            if (barrier(state, x, y) & dir)
                draw_barrier(fe, x, y, dir, phase);
    }

    draw_update(fe, bx, by, TILE_SIZE+TILE_BORDER, TILE_SIZE+TILE_BORDER);
}

void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                 game_state *state, float t, float ft)
{
    int x, y, tx, ty, frame;
    unsigned char *active;
    float angle = 0.0;

    /*
     * Clear the screen and draw the exterior barrier lines if this
     * is our first call.
     */
    if (!ds->started) {
        int phase;

        ds->started = TRUE;

        draw_rect(fe, 0, 0, 
                  WINDOW_OFFSET * 2 + TILE_SIZE * state->width + TILE_BORDER,
                  WINDOW_OFFSET * 2 + TILE_SIZE * state->height + TILE_BORDER,
                  COL_BACKGROUND);
        draw_update(fe, 0, 0, 
                    WINDOW_OFFSET*2 + TILE_SIZE*state->width + TILE_BORDER,
                    WINDOW_OFFSET*2 + TILE_SIZE*state->height + TILE_BORDER);

        for (phase = 0; phase < 2; phase++) {

            for (x = 0; x < ds->width; x++) {
                if (barrier(state, x, 0) & UL)
                    draw_barrier_corner(fe, x, -1, LD, phase);
                if (barrier(state, x, 0) & RU)
                    draw_barrier_corner(fe, x, -1, DR, phase);
                if (barrier(state, x, 0) & U)
                    draw_barrier(fe, x, -1, D, phase);
                if (barrier(state, x, ds->height-1) & DR)
                    draw_barrier_corner(fe, x, ds->height, RU, phase);
                if (barrier(state, x, ds->height-1) & LD)
                    draw_barrier_corner(fe, x, ds->height, UL, phase);
                if (barrier(state, x, ds->height-1) & D)
                    draw_barrier(fe, x, ds->height, U, phase);
            }

            for (y = 0; y < ds->height; y++) {
                if (barrier(state, 0, y) & UL)
                    draw_barrier_corner(fe, -1, y, RU, phase);
                if (barrier(state, 0, y) & LD)
                    draw_barrier_corner(fe, -1, y, DR, phase);
                if (barrier(state, 0, y) & L)
                    draw_barrier(fe, -1, y, R, phase);
                if (barrier(state, ds->width-1, y) & RU)
                    draw_barrier_corner(fe, ds->width, y, UL, phase);
                if (barrier(state, ds->width-1, y) & DR)
                    draw_barrier_corner(fe, ds->width, y, LD, phase);
                if (barrier(state, ds->width-1, y) & R)
                    draw_barrier(fe, ds->width, y, L, phase);
            }
        }
    }

    tx = ty = -1;
    if (oldstate && (t < ROTATE_TIME)) {
        /*
         * We're animating a tile rotation. Find the turning tile,
         * if any.
         */
        for (x = 0; x < oldstate->width; x++)
            for (y = 0; y < oldstate->height; y++)
                if ((tile(oldstate, x, y) ^ tile(state, x, y)) & 0xF) {
                    tx = x, ty = y;
                    goto break_label;  /* leave both loops at once */
                }
        break_label:

        if (tx >= 0) {
            if (tile(state, tx, ty) == ROT(tile(oldstate, tx, ty),
                                           state->last_rotate_dir))
                angle = state->last_rotate_dir * 90.0F * (t / ROTATE_TIME);
            else
                angle = state->last_rotate_dir * -90.0F * (t / ROTATE_TIME);
            state = oldstate;
        }
    }
    
    frame = -1;
    if (ft > 0) {
        /*
         * We're animating a completion flash. Find which frame
         * we're at.
         */
        frame = (int)(ft / FLASH_FRAME);
    }

    /*
     * Draw any tile which differs from the way it was last drawn.
     */
    active = compute_active(state);

    for (x = 0; x < ds->width; x++)
        for (y = 0; y < ds->height; y++) {
            unsigned char c = tile(state, x, y) | index(state, active, x, y);

            /*
             * In a completion flash, we adjust the LOCKED bit
             * depending on our distance from the centre point and
             * the frame number.
             */
            if (frame >= 0) {
                int xdist, ydist, dist;
                xdist = (x < state->cx ? state->cx - x : x - state->cx);
                ydist = (y < state->cy ? state->cy - y : y - state->cy);
                dist = (xdist > ydist ? xdist : ydist);

                if (frame >= dist && frame < dist+4) {
                    int lock = (frame - dist) & 1;
                    lock = lock ? LOCKED : 0;
                    c = (c &~ LOCKED) | lock;
                }
            }

            if (index(state, ds->visible, x, y) != c ||
                index(state, ds->visible, x, y) == 0xFF ||
                (x == tx && y == ty)) {
                draw_tile(fe, state, x, y, c,
                          (x == tx && y == ty ? angle : 0.0F));
                if (x == tx && y == ty)
                    index(state, ds->visible, x, y) = 0xFF;
                else
                    index(state, ds->visible, x, y) = c;
            }
        }

    /*
     * Update the status bar.
     */
    {
	char statusbuf[256];
	int i, n, a;

	n = state->width * state->height;
	for (i = a = 0; i < n; i++)
	    if (active[i])
		a++;

	sprintf(statusbuf, "%sActive: %d/%d",
		(state->completed ? "COMPLETED! " : ""), a, n);

	status_bar(fe, statusbuf);
    }

    sfree(active);
}

float game_anim_length(game_state *oldstate, game_state *newstate)
{
    int x, y;

    /*
     * If there's a tile which has been rotated, allow time to
     * animate its rotation.
     */
    for (x = 0; x < oldstate->width; x++)
        for (y = 0; y < oldstate->height; y++)
            if ((tile(oldstate, x, y) ^ tile(newstate, x, y)) & 0xF) {
                return ROTATE_TIME;
            }

    return 0.0F;
}

float game_flash_length(game_state *oldstate, game_state *newstate)
{
    /*
     * If the game has just been completed, we display a completion
     * flash.
     */
    if (!oldstate->completed && newstate->completed) {
        int size;
        size = 0;
        if (size < newstate->cx+1)
            size = newstate->cx+1;
        if (size < newstate->cy+1)
            size = newstate->cy+1;
        if (size < newstate->width - newstate->cx)
            size = newstate->width - newstate->cx;
        if (size < newstate->height - newstate->cy)
            size = newstate->height - newstate->cy;
        return FLASH_FRAME * (size+4);
    }

    return 0.0F;
}

int game_wants_statusbar(void)
{
    return TRUE;
}