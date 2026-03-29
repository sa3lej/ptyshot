/*
 * test_font.c — Unit tests for font glyph lookup and rendering.
 */

#define PTYSHOT_TEST
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../ptyshot.c"
#include "test.h"

/* --- Space glyph is all zeros --- */
static void
test_space_glyph(void)
{
	const unsigned char *g = font8x16_glyph(' ');
	int all_zero = 1;
	for (int i = 0; i < 16; i++) {
		if (g[i] != 0) { all_zero = 0; break; }
	}
	ASSERT_TRUE("space: all zero", all_zero);
	TEST_PASS("space glyph is all zeros");
}

/* --- Known glyph data for 'A' --- */
static void
test_known_glyph_a(void)
{
	const unsigned char *g = font8x16_glyph('A');
	/* 'A' in VGA 8x16: row 0-1 are 0, row 2 should have the top of A. */
	/* Verify it's not tofu and not space. */
	ASSERT_TRUE("A: not tofu", g != font_tofu);
	ASSERT_TRUE("A: not space", g != font8x16_glyph(' '));
	/* Row 2 of 'A' in standard VGA font is 0x10 (narrow peak). */
	/* Just verify some rows are nonzero (font may vary slightly). */
	int has_pixels = 0;
	for (int i = 0; i < 16; i++) {
		if (g[i] != 0) { has_pixels = 1; break; }
	}
	ASSERT_TRUE("A: has pixels", has_pixels);
	TEST_PASS("known glyph data for 'A'");
}

/* --- ASCII range coverage (0x20-0x7E all have non-tofu glyphs) --- */
static void
test_ascii_range(void)
{
	for (uint32_t cp = 0x21; cp <= 0x7E; cp++) {
		const unsigned char *g = font8x16_glyph(cp);
		if (g == font_tofu) {
			char msg[64];
			snprintf(msg, sizeof msg, "codepoint 0x%02X returned tofu", cp);
			TEST_FAIL("ascii range", msg);
			return;
		}
	}
	TEST_PASS("ASCII 0x21-0x7E all have non-tofu glyphs");
}

/* --- Box drawing horizontal line (U+2500) --- */
static void
test_box_drawing_horizontal(void)
{
	const unsigned char *g = font8x16_glyph(0x2500);
	ASSERT_TRUE("box ─: not tofu", g != font_tofu);
	/* Horizontal line: one row should be 0xFF (all pixels lit). */
	int found_ff = 0;
	for (int i = 0; i < 16; i++) {
		if (g[i] == 0xFF) { found_ff = 1; break; }
	}
	ASSERT_TRUE("box ─: has full-width row", found_ff);
	TEST_PASS("box drawing U+2500 (horizontal line)");
}

/* --- Full block (U+2588) --- */
static void
test_full_block(void)
{
	const unsigned char *g = font8x16_glyph(0x2588);
	ASSERT_TRUE("block █: not tofu", g != font_tofu);
	/* Full block: all 16 rows should be 0xFF. */
	int all_ff = 1;
	for (int i = 0; i < 16; i++) {
		if (g[i] != 0xFF) { all_ff = 0; break; }
	}
	ASSERT_TRUE("block █: all rows 0xFF", all_ff);
	TEST_PASS("full block U+2588");
}

/* --- Tofu for unknown codepoint --- */
static void
test_tofu_unknown(void)
{
	const unsigned char *g = font8x16_glyph(0xFFFF);
	ASSERT_TRUE("tofu: returns font_tofu", g == font_tofu);

	/* Tofu is an open rectangle: first few rows 0, then 0x7E border. */
	ASSERT_EQ("tofu: row 2 is 0x7E", 0x7E, g[2]);
	ASSERT_EQ("tofu: row 3 is 0x42", 0x42, g[3]);
	ASSERT_EQ("tofu: row 11 is 0x7E", 0x7E, g[11]);
	TEST_PASS("tofu for unknown codepoint 0xFFFF");
}

/* --- Latin-1 supplement has glyphs --- */
static void
test_latin1_supplement(void)
{
	/* Ä (U+00C4) */
	const unsigned char *g = font8x16_glyph(0x00C4);
	ASSERT_TRUE("Ä: not tofu", g != font_tofu);

	/* é (U+00E9) */
	g = font8x16_glyph(0x00E9);
	ASSERT_TRUE("é: not tofu", g != font_tofu);

	/* ñ (U+00F1) */
	g = font8x16_glyph(0x00F1);
	ASSERT_TRUE("ñ: not tofu", g != font_tofu);
	TEST_PASS("Latin-1 supplement characters have glyphs");
}

/* --- Box drawing vertical line (U+2502) --- */
static void
test_box_drawing_vertical(void)
{
	const unsigned char *g = font8x16_glyph(0x2502);
	ASSERT_TRUE("box │: not tofu", g != font_tofu);
	/* Vertical line: every row should have 0x18 (center 2 pixels). */
	ASSERT_EQ("box │: row 0", 0x18, g[0]);
	ASSERT_EQ("box │: row 8", 0x18, g[8]);
	ASSERT_EQ("box │: row 15", 0x18, g[15]);
	TEST_PASS("box drawing U+2502 (vertical line)");
}

/* --- Shade characters --- */
static void
test_shades(void)
{
	const unsigned char *g;

	g = font8x16_glyph(0x2591); /* light shade */
	ASSERT_TRUE("░: not tofu", g != font_tofu);

	g = font8x16_glyph(0x2592); /* medium shade */
	ASSERT_TRUE("▒: not tofu", g != font_tofu);

	g = font8x16_glyph(0x2593); /* dark shade */
	ASSERT_TRUE("▓: not tofu", g != font_tofu);

	TEST_PASS("shade characters (░▒▓)");
}

/* --- Rounded box-drawing corners (╭╮╰╯) --- */
static void
test_rounded_corners(void)
{
	const unsigned char *g;

	g = font8x16_glyph(0x256D); /* ╭ */
	ASSERT_TRUE("╭: not tofu", g != font_tofu);

	g = font8x16_glyph(0x256E); /* ╮ */
	ASSERT_TRUE("╮: not tofu", g != font_tofu);

	g = font8x16_glyph(0x256F); /* ╯ */
	ASSERT_TRUE("╯: not tofu", g != font_tofu);

	g = font8x16_glyph(0x2570); /* ╰ */
	ASSERT_TRUE("╰: not tofu", g != font_tofu);

	TEST_PASS("rounded box-drawing corners (╭╮╰╯)");
}

/* --- Heavy box-drawing --- */
static void
test_heavy_box_drawing(void)
{
	ASSERT_TRUE("━: not tofu", font8x16_glyph(0x2501) != font_tofu);
	ASSERT_TRUE("┃: not tofu", font8x16_glyph(0x2503) != font_tofu);
	ASSERT_TRUE("┏: not tofu", font8x16_glyph(0x250F) != font_tofu);
	ASSERT_TRUE("┓: not tofu", font8x16_glyph(0x2513) != font_tofu);
	ASSERT_TRUE("┗: not tofu", font8x16_glyph(0x2517) != font_tofu);
	ASSERT_TRUE("┛: not tofu", font8x16_glyph(0x251B) != font_tofu);
	ASSERT_TRUE("╋: not tofu", font8x16_glyph(0x254B) != font_tofu);
	TEST_PASS("heavy box-drawing (━┃┏┓┗┛╋)");
}

/* --- Arrows --- */
static void
test_arrows(void)
{
	ASSERT_TRUE("←: not tofu", font8x16_glyph(0x2190) != font_tofu);
	ASSERT_TRUE("↑: not tofu", font8x16_glyph(0x2191) != font_tofu);
	ASSERT_TRUE("→: not tofu", font8x16_glyph(0x2192) != font_tofu);
	ASSERT_TRUE("↓: not tofu", font8x16_glyph(0x2193) != font_tofu);
	TEST_PASS("arrows (←↑→↓)");
}

/* --- Powerline symbols --- */
static void
test_powerline(void)
{
	ASSERT_TRUE("E0B0: not tofu", font8x16_glyph(0xE0B0) != font_tofu);
	ASSERT_TRUE("E0B1: not tofu", font8x16_glyph(0xE0B1) != font_tofu);
	ASSERT_TRUE("E0B2: not tofu", font8x16_glyph(0xE0B2) != font_tofu);
	ASSERT_TRUE("E0B3: not tofu", font8x16_glyph(0xE0B3) != font_tofu);
	TEST_PASS("powerline symbols (E0B0-E0B3)");
}

/* --- Misc symbols --- */
static void
test_misc_symbols(void)
{
	ASSERT_TRUE("•: not tofu", font8x16_glyph(0x2022) != font_tofu);
	ASSERT_TRUE("…: not tofu", font8x16_glyph(0x2026) != font_tofu);
	ASSERT_TRUE("✓: not tofu", font8x16_glyph(0x2713) != font_tofu);
	ASSERT_TRUE("✗: not tofu", font8x16_glyph(0x2717) != font_tofu);
	ASSERT_TRUE("●: not tofu", font8x16_glyph(0x25CF) != font_tofu);
	ASSERT_TRUE("○: not tofu", font8x16_glyph(0x25CB) != font_tofu);
	ASSERT_TRUE("◆: not tofu", font8x16_glyph(0x25C6) != font_tofu);
	ASSERT_TRUE("■: not tofu", font8x16_glyph(0x25A0) != font_tofu);
	TEST_PASS("misc symbols (•…✓✗●○◆■)");
}

/* --- Double box-drawing --- */
static void
test_double_box_drawing(void)
{
	ASSERT_TRUE("╔: not tofu", font8x16_glyph(0x2554) != font_tofu);
	ASSERT_TRUE("╗: not tofu", font8x16_glyph(0x2557) != font_tofu);
	ASSERT_TRUE("╚: not tofu", font8x16_glyph(0x255A) != font_tofu);
	ASSERT_TRUE("╝: not tofu", font8x16_glyph(0x255D) != font_tofu);
	ASSERT_TRUE("╠: not tofu", font8x16_glyph(0x2560) != font_tofu);
	ASSERT_TRUE("╣: not tofu", font8x16_glyph(0x2563) != font_tofu);
	ASSERT_TRUE("╦: not tofu", font8x16_glyph(0x2566) != font_tofu);
	ASSERT_TRUE("╩: not tofu", font8x16_glyph(0x2569) != font_tofu);
	ASSERT_TRUE("╬: not tofu", font8x16_glyph(0x256C) != font_tofu);
	TEST_PASS("double box-drawing corners and junctions");
}

/* --- Null/zero codepoint returns space --- */
static void
test_null_glyph(void)
{
	const unsigned char *g = font8x16_glyph(0);
	const unsigned char *space = font8x16_glyph(' ');
	ASSERT_TRUE("null: returns space glyph", g == space);
	TEST_PASS("null codepoint returns space glyph");
}

int
main(void)
{
	printf("=== Font Tests ===\n");

	RUN_TEST(test_space_glyph);
	RUN_TEST(test_known_glyph_a);
	RUN_TEST(test_ascii_range);
	RUN_TEST(test_box_drawing_horizontal);
	RUN_TEST(test_box_drawing_vertical);
	RUN_TEST(test_full_block);
	RUN_TEST(test_tofu_unknown);
	RUN_TEST(test_latin1_supplement);
	RUN_TEST(test_shades);
	RUN_TEST(test_rounded_corners);
	RUN_TEST(test_heavy_box_drawing);
	RUN_TEST(test_arrows);
	RUN_TEST(test_powerline);
	RUN_TEST(test_misc_symbols);
	RUN_TEST(test_double_box_drawing);
	RUN_TEST(test_null_glyph);

	TEST_SUMMARY();
}
