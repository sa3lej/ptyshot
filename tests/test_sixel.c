/*
 * test_sixel.c — Unit tests for the SIXEL decoder.
 */

#define PTYSHOT_TEST
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../ptyshot.c"
#include "test.h"

/* Helper: get pixel (r,g,b,a) from a sixel_img at position (x,y). */
static void
sixel_pixel(struct sixel_img *img, int x, int y,
    uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
	int off = (y * img->w + x) * 4;
	*r = img->pixels[off + 0];
	*g = img->pixels[off + 1];
	*b = img->pixels[off + 2];
	*a = img->pixels[off + 3];
}

/* --- Empty/invalid input returns no image --- */
static void
test_sixel_empty(void)
{
	struct sixel_img img = sixel_decode("", 0);
	ASSERT_TRUE("empty: no pixels", img.pixels == NULL);
	TEST_PASS("empty input returns no image");
}

/* --- Missing 'q' prefix returns no image --- */
static void
test_sixel_no_q(void)
{
	const char *data = "not a sixel";
	struct sixel_img img = sixel_decode(data, strlen(data));
	ASSERT_TRUE("no-q: no pixels", img.pixels == NULL);
	TEST_PASS("missing 'q' prefix returns no image");
}

/* --- Single color, single column --- */
static void
test_sixel_single_column(void)
{
	/*
	 * SIXEL data: q#0;2;100;0;0~
	 * #0;2;100;0;0 = define color 0 as RGB(100%,0%,0%) = red
	 * ~ = sixel char 0x7E = all 6 bits set (0x3F = 63), so all 6 pixels on
	 */
	const char *data = "q#0;2;100;0;0~";
	struct sixel_img img = sixel_decode(data, strlen(data));
	ASSERT_TRUE("1col: has pixels", img.pixels != NULL);
	ASSERT_TRUE("1col: w >= 1", img.w >= 1);
	ASSERT_TRUE("1col: h >= 6", img.h >= 6);

	/* All 6 pixels in column 0 should be red. */
	uint8_t r, g, b, a;
	sixel_pixel(&img, 0, 0, &r, &g, &b, &a);
	ASSERT_EQ("1col: pixel(0,0) R", 0xFF, r);
	ASSERT_EQ("1col: pixel(0,0) G", 0x00, g);
	ASSERT_EQ("1col: pixel(0,0) B", 0x00, b);

	sixel_pixel(&img, 0, 5, &r, &g, &b, &a);
	ASSERT_EQ("1col: pixel(0,5) R", 0xFF, r);

	TEST_PASS("single color, single column (red ~)");
	free(img.pixels);
}

/* --- Repeat operator (!N) --- */
static void
test_sixel_repeat(void)
{
	/* q#0;2;0;100;0!5~ = 5 columns of green, all bits set. */
	const char *data = "q#0;2;0;100;0!5~";
	struct sixel_img img = sixel_decode(data, strlen(data));
	ASSERT_TRUE("repeat: has pixels", img.pixels != NULL);
	ASSERT_TRUE("repeat: w >= 5", img.w >= 5);

	/* Check columns 0 and 4 are green. */
	uint8_t r, g, b, a;
	sixel_pixel(&img, 0, 0, &r, &g, &b, &a);
	ASSERT_EQ("repeat: col0 G", 0xFF, g);
	ASSERT_EQ("repeat: col0 R", 0x00, r);

	sixel_pixel(&img, 4, 0, &r, &g, &b, &a);
	ASSERT_EQ("repeat: col4 G", 0xFF, g);

	TEST_PASS("repeat operator (!5~)");
	free(img.pixels);
}

/* --- Newline (-) creates second band --- */
static void
test_sixel_newline(void)
{
	/* Two bands of 6 pixels each = 12 pixels high.
	 * Band 1: blue ~, Band 2: blue ~ */
	const char *data = "q#0;2;0;0;100~-~";
	struct sixel_img img = sixel_decode(data, strlen(data));
	ASSERT_TRUE("nl: has pixels", img.pixels != NULL);
	ASSERT_TRUE("nl: h >= 12", img.h >= 12);

	/* Check pixel in second band. */
	uint8_t r, g, b, a;
	sixel_pixel(&img, 0, 6, &r, &g, &b, &a);
	ASSERT_EQ("nl: band2 B", 0xFF, b);
	ASSERT_EQ("nl: band2 R", 0x00, r);

	TEST_PASS("newline (-) creates second band");
	free(img.pixels);
}

/* --- Carriage return ($) resets x position --- */
static void
test_sixel_cr(void)
{
	/*
	 * Draw red in column 0, then $ (CR), then define green color and
	 * draw green in column 0. Green should overwrite red.
	 */
	const char *data = "q#0;2;100;0;0~$#1;2;0;100;0~";
	struct sixel_img img = sixel_decode(data, strlen(data));
	ASSERT_TRUE("cr: has pixels", img.pixels != NULL);

	/* Column 0 should be green (overwritten). */
	uint8_t r, g, b, a;
	sixel_pixel(&img, 0, 0, &r, &g, &b, &a);
	ASSERT_EQ("cr: overwritten G", 0xFF, g);
	ASSERT_EQ("cr: overwritten R", 0x00, r);

	TEST_PASS("carriage return ($) resets x, allows overwrite");
	free(img.pixels);
}

/* --- Multiple colors --- */
static void
test_sixel_multi_color(void)
{
	/*
	 * Define red (#0) and blue (#1).
	 * Draw red in col 0, blue in col 1.
	 */
	const char *data = "q#0;2;100;0;0#1;2;0;0;100#0~#1~";
	struct sixel_img img = sixel_decode(data, strlen(data));
	ASSERT_TRUE("multi: has pixels", img.pixels != NULL);
	ASSERT_TRUE("multi: w >= 2", img.w >= 2);

	uint8_t r, g, b, a;
	/* Col 0 should be red. */
	sixel_pixel(&img, 0, 0, &r, &g, &b, &a);
	ASSERT_EQ("multi: col0 R", 0xFF, r);
	ASSERT_EQ("multi: col0 B", 0x00, b);

	/* Col 1 should be blue. */
	sixel_pixel(&img, 1, 0, &r, &g, &b, &a);
	ASSERT_EQ("multi: col1 R", 0x00, r);
	ASSERT_EQ("multi: col1 B", 0xFF, b);

	TEST_PASS("multiple colors in one SIXEL image");
	free(img.pixels);
}

/* --- Raster attributes set dimensions --- */
static void
test_sixel_raster_attrs(void)
{
	/*
	 * "1;1;10;12 means: Pan=1, Pad=1, width=10, height=12
	 * Then fill with data.
	 */
	const char *data = "q\"1;1;10;12#0;2;50;50;50!10~-!10~";
	struct sixel_img img = sixel_decode(data, strlen(data));
	ASSERT_TRUE("raster: has pixels", img.pixels != NULL);
	/* Width should be at least 10, height at least 12. */
	ASSERT_TRUE("raster: w >= 10", img.w >= 10);
	ASSERT_TRUE("raster: h >= 12", img.h >= 12);

	TEST_PASS("raster attributes set dimensions");
	free(img.pixels);
}

/* --- Partial sixel character (not all 6 bits set) --- */
static void
test_sixel_partial_bits(void)
{
	/*
	 * Sixel char '?' = 0x3F - 0x3F = 0 (no bits set)
	 * Sixel char '@' = 0x40 - 0x3F = 1 (bit 0 set = top pixel only)
	 * Sixel char 'A' = 0x41 - 0x3F = 2 (bit 1 set = second pixel only)
	 */
	const char *data = "q#0;2;100;100;100@";
	struct sixel_img img = sixel_decode(data, strlen(data));
	ASSERT_TRUE("partial: has pixels", img.pixels != NULL);

	/* '@' = bit 0 only. Pixel (0,0) should be white, (0,1) should be 0. */
	uint8_t r, g, b, a;
	sixel_pixel(&img, 0, 0, &r, &g, &b, &a);
	ASSERT_EQ("partial: pixel(0,0) R", 0xFF, r);

	/* Pixel (0,1) should NOT be set by this color (remains 0/transparent). */
	sixel_pixel(&img, 0, 1, &r, &g, &b, &a);
	ASSERT_EQ("partial: pixel(0,1) R", 0x00, r);

	TEST_PASS("partial sixel bits (@ = bit 0 only)");
	free(img.pixels);
}

int
main(void)
{
	printf("=== SIXEL Tests ===\n");

	RUN_TEST(test_sixel_empty);
	RUN_TEST(test_sixel_no_q);
	RUN_TEST(test_sixel_single_column);
	RUN_TEST(test_sixel_repeat);
	RUN_TEST(test_sixel_newline);
	RUN_TEST(test_sixel_cr);
	RUN_TEST(test_sixel_multi_color);
	RUN_TEST(test_sixel_raster_attrs);
	RUN_TEST(test_sixel_partial_bits);

	TEST_SUMMARY();
}
