/*
 * test_render.c — Unit tests for the rendering pipeline.
 *
 * Tests that cell state is correctly composited into RGBA pixels.
 */

#define PTYSHOT_TEST
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../ptyshot.c"
#include "test.h"

/* Get pixel at (px_x, px_y) in RGBA buffer of width pw. */
static void
get_pixel(const unsigned char *px, int pw, int px_x, int px_y,
    uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
	int off = (px_y * pw + px_x) * 4;
	*r = px[off + 0];
	*g = px[off + 1];
	*b = px[off + 2];
	*a = px[off + 3];
}

/* --- Empty screen has default background --- */
static void
test_empty_background(void)
{
	struct term *t = test_term_new(4, 2);
	t->cursor_visible = 0; /* hide cursor so it doesn't affect pixel checks */
	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("bg: render succeeded", px != NULL);
	ASSERT_EQ("bg: width", 4 * CELL_W, pw);
	ASSERT_EQ("bg: height", 2 * CELL_H, ph);

	/* Check a pixel in the middle. */
	uint8_t r, g, b, a;
	get_pixel(px, pw, 5, 5, &r, &g, &b, &a);
	ASSERT_EQ("bg: R", 0x0C, r);
	ASSERT_EQ("bg: G", 0x0C, g);
	ASSERT_EQ("bg: B", 0x16, b);
	ASSERT_EQ("bg: A", 0xFF, a);

	TEST_PASS("empty screen has default background");
	free(px);
	test_term_free(t);
}

/* --- Foreground color applied to glyph pixels --- */
static void
test_fg_color_rendering(void)
{
	struct term *t = test_term_new(4, 2);
	t->cursor_visible = 0;
	/* Place a full block (U+2588) — all pixels are foreground. */
	struct cell *c = cell_at(t, 0, 0);
	c->ch = 0x2588; /* full block */
	c->fg = 0xFF0000; /* red */

	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("fg: render succeeded", px != NULL);

	/* Every pixel in cell (0,0) should be red. */
	uint8_t r, g, b, a;
	get_pixel(px, pw, 3, 3, &r, &g, &b, &a);
	ASSERT_EQ("fg: R=0xFF", 0xFF, r);
	ASSERT_EQ("fg: G=0x00", 0x00, g);
	ASSERT_EQ("fg: B=0x00", 0x00, b);

	TEST_PASS("foreground color applied to glyph pixels");
	free(px);
	test_term_free(t);
}

/* --- Background color in non-glyph area --- */
static void
test_bg_color_rendering(void)
{
	struct term *t = test_term_new(4, 2);
	t->cursor_visible = 0;
	/* Space character — all pixels are background. */
	struct cell *c = cell_at(t, 0, 0);
	c->ch = ' ';
	c->bg = 0x00FF00; /* green */

	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("bg-color: render succeeded", px != NULL);

	uint8_t r, g, b, a;
	get_pixel(px, pw, 3, 3, &r, &g, &b, &a);
	ASSERT_EQ("bg-color: R=0x00", 0x00, r);
	ASSERT_EQ("bg-color: G=0xFF", 0xFF, g);
	ASSERT_EQ("bg-color: B=0x00", 0x00, b);

	TEST_PASS("background color in non-glyph area");
	free(px);
	test_term_free(t);
}

/* --- ATTR_REVERSE swaps fg/bg --- */
static void
test_reverse_rendering(void)
{
	struct term *t = test_term_new(4, 2);
	t->cursor_visible = 0;
	/* Full block with reverse: glyph pixels should use bg color. */
	struct cell *c = cell_at(t, 0, 0);
	c->ch = 0x2588; /* full block */
	c->fg = 0xFF0000; /* red */
	c->bg = 0x0000FF; /* blue */
	c->attr = ATTR_REVERSE;

	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("rev: render succeeded", px != NULL);

	/* With reverse, glyph-lit pixels use what was bg (blue). */
	uint8_t r, g, b, a;
	get_pixel(px, pw, 3, 3, &r, &g, &b, &a);
	ASSERT_EQ("rev: R=0x00", 0x00, r);
	ASSERT_EQ("rev: G=0x00", 0x00, g);
	ASSERT_EQ("rev: B=0xFF", 0xFF, b);

	TEST_PASS("ATTR_REVERSE swaps fg/bg");
	free(px);
	test_term_free(t);
}

/* --- ATTR_BRIGHT brightens fg --- */
static void
test_bright_rendering(void)
{
	struct term *t = test_term_new(4, 2);
	t->cursor_visible = 0;
	struct cell *c = cell_at(t, 0, 0);
	c->ch = 0x2588; /* full block */
	c->fg = 0x606060;
	c->attr = ATTR_BRIGHT;

	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("bright: render succeeded", px != NULL);

	uint8_t r, g, b, a;
	get_pixel(px, pw, 3, 3, &r, &g, &b, &a);

	/* Bright formula: r += (255-r)/3.  0x60 + (255-0x60)/3 = 96 + 53 = 149 = 0x95 */
	ASSERT_EQ("bright: R brightened", 0x95, r);

	TEST_PASS("ATTR_BRIGHT brightens foreground");
	free(px);
	test_term_free(t);
}

/* --- ATTR_DIM halves fg --- */
static void
test_dim_rendering(void)
{
	struct term *t = test_term_new(4, 2);
	t->cursor_visible = 0;
	struct cell *c = cell_at(t, 0, 0);
	c->ch = 0x2588;
	c->fg = 0x808080;
	c->attr = ATTR_DIM;

	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("dim: render succeeded", px != NULL);

	uint8_t r, g, b, a;
	get_pixel(px, pw, 3, 3, &r, &g, &b, &a);
	ASSERT_EQ("dim: R halved", 0x40, r);
	ASSERT_EQ("dim: G halved", 0x40, g);
	ASSERT_EQ("dim: B halved", 0x40, b);

	TEST_PASS("ATTR_DIM halves foreground");
	free(px);
	test_term_free(t);
}

/* --- ATTR_UNDERLINE draws line at row 14 --- */
static void
test_underline_rendering(void)
{
	struct term *t = test_term_new(4, 2);
	t->cursor_visible = 0;
	struct cell *c = cell_at(t, 0, 0);
	c->ch = ' '; /* space so glyph doesn't interfere */
	c->fg = 0xFF0000;
	c->bg = 0x000000;
	c->attr = ATTR_UNDERLINE;

	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("ul: render succeeded", px != NULL);

	/* Row 14 within cell (0,0) should be foreground-colored. */
	uint8_t r, g, b, a;
	get_pixel(px, pw, 3, 14, &r, &g, &b, &a);
	ASSERT_EQ("ul: R at row 14", 0xFF, r);
	ASSERT_EQ("ul: G at row 14", 0x00, g);

	/* Row 5 should be background (no glyph, no underline). */
	get_pixel(px, pw, 3, 5, &r, &g, &b, &a);
	ASSERT_EQ("ul: row 5 is bg", 0x00, r);

	TEST_PASS("ATTR_UNDERLINE draws at row 14");
	free(px);
	test_term_free(t);
}

/* --- Render dimensions correct --- */
static void
test_render_dimensions(void)
{
	struct term *t = test_term_new(80, 24);
	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("dims: render succeeded", px != NULL);
	ASSERT_EQ("dims: width", 80 * 8, pw);
	ASSERT_EQ("dims: height", 24 * 16, ph);

	TEST_PASS("render dimensions (80x24 -> 640x384)");
	free(px);
	test_term_free(t);
}

/* --- Cursor visible vs hidden rendering --- */
static void
test_cursor_rendering(void)
{
	/* Render with cursor visible at (0,0) on an empty screen. */
	struct term *t1 = test_term_new(4, 2);
	t1->cursor_visible = 1;
	int pw1, ph1;
	unsigned char *px1 = render(t1, &pw1, &ph1);
	ASSERT_TRUE("cursor-vis: render ok", px1 != NULL);

	/* Render with cursor hidden. */
	struct term *t2 = test_term_new(4, 2);
	t2->cursor_visible = 0;
	int pw2, ph2;
	unsigned char *px2 = render(t2, &pw2, &ph2);
	ASSERT_TRUE("cursor-hid: render ok", px2 != NULL);

	/* The two images should differ at the cursor position. */
	int diffs = 0;
	for (int y = 0; y < CELL_H; y++) {
		for (int x = 0; x < CELL_W; x++) {
			int off = (y * pw1 + x) * 4;
			if (px1[off] != px2[off] || px1[off+1] != px2[off+1] ||
			    px1[off+2] != px2[off+2])
				diffs++;
		}
	}
	ASSERT_TRUE("cursor: visible cursor differs from hidden", diffs > 0);

	/* Pixels outside cell (0,0) should be identical. */
	int outer_diffs = 0;
	for (int y = 0; y < ph1; y++) {
		for (int x = CELL_W; x < pw1; x++) {
			int off = (y * pw1 + x) * 4;
			if (px1[off] != px2[off] || px1[off+1] != px2[off+1] ||
			    px1[off+2] != px2[off+2])
				outer_diffs++;
		}
	}
	ASSERT_EQ("cursor: outside cursor cell identical", 0, outer_diffs);

	TEST_PASS("cursor visibility: visible differs from hidden at cursor pos");
	free(px1); free(px2);
	test_term_free(t1); test_term_free(t2);
}

/* --- ATTR_ITALIC shifts top rows rightward --- */
static void
test_italic_rendering(void)
{
	struct term *t = test_term_new(4, 2);
	t->cursor_visible = 0;
	/* Use full block so we can see where pixels land. */
	struct cell *c = cell_at(t, 0, 0);
	c->ch = 0x2588;
	c->fg = 0xFF0000;
	c->bg = 0x000000;
	c->attr = ATTR_ITALIC;

	struct cell *c2 = cell_at(t, 0, 0);
	/* Also set a non-italic full block in cell (1,0) for comparison. */
	struct cell *nc = cell_at(t, 1, 0);
	nc->ch = 0x2588;
	nc->fg = 0x00FF00;
	nc->bg = 0x000000;
	nc->attr = 0;
	(void)c2;

	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("italic: render succeeded", px != NULL);

	/* For a full block with italic, the top row pixels should be shifted
	 * rightward compared to the bottom row. At row 0, shift = (15-0)/5 = 3.
	 * At row 15, shift = 0. So pixel (0, 0) should be background (shifted away),
	 * while pixel (0, 15) should be foreground (no shift). */
	uint8_t r, g, b, a;

	/* Bottom row (gy=15): shift=0, pixel (0,15) should be red (fg). */
	get_pixel(px, pw, 0, 15, &r, &g, &b, &a);
	ASSERT_EQ("italic: bottom-left R", 0xFF, r);

	/* Top row (gy=0): shift=3, pixel (0,0) retains canvas bg (0x0C0C16). */
	get_pixel(px, pw, 0, 0, &r, &g, &b, &a);
	ASSERT_EQ("italic: top-left is canvas bg R", 0x0C, r);

	/* But pixel (3,0) should be red (shifted to here). */
	get_pixel(px, pw, 3, 0, &r, &g, &b, &a);
	ASSERT_EQ("italic: top-shifted R", 0xFF, r);

	TEST_PASS("ATTR_ITALIC: shear rendering shifts top rows right");
	free(px);
	test_term_free(t);
}

/* --- Full pipeline: feed text, render, verify pixels --- */
static void
test_full_pipeline(void)
{
	struct term *t = test_term_new(10, 3);
	t->cursor_visible = 0;
	test_feed_str(t, "\033[31m\033[42m");
	/* Place full block with red fg on green bg. */
	struct cell *c = cell_at(t, 0, 0);
	c->ch = 0x2588;
	c->fg = palette256[1]; /* red */
	c->bg = palette256[2]; /* green */

	int pw, ph;
	unsigned char *px = render(t, &pw, &ph);
	ASSERT_TRUE("pipe: render succeeded", px != NULL);

	/* Full block pixel should be red foreground. */
	uint8_t r, g, b, a;
	get_pixel(px, pw, 0, 0, &r, &g, &b, &a);
	ASSERT_EQ("pipe: R", (palette256[1] >> 16) & 0xFF, r);
	ASSERT_EQ("pipe: G", (palette256[1] >> 8) & 0xFF, g);
	ASSERT_EQ("pipe: B", palette256[1] & 0xFF, b);

	TEST_PASS("full pipeline: text -> cells -> pixels");
	free(px);
	test_term_free(t);
}

int
main(void)
{
	printf("=== Render Tests ===\n");

	RUN_TEST(test_empty_background);
	RUN_TEST(test_fg_color_rendering);
	RUN_TEST(test_bg_color_rendering);
	RUN_TEST(test_reverse_rendering);
	RUN_TEST(test_bright_rendering);
	RUN_TEST(test_dim_rendering);
	RUN_TEST(test_underline_rendering);
	RUN_TEST(test_render_dimensions);
	RUN_TEST(test_cursor_rendering);
	RUN_TEST(test_italic_rendering);
	RUN_TEST(test_full_pipeline);

	TEST_SUMMARY();
}
