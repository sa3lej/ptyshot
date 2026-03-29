/*
 * test_vt.c — Unit tests for the VT/ANSI parser in ptyshot.
 *
 * Includes ptyshot.c directly to access static functions.
 */

#define PTYSHOT_TEST
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../ptyshot.c"
#include "test.h"

/* --- Plain text placement --- */
static void
test_ascii_text(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "Hello");

	ASSERT_EQ("ascii: H", 'H', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("ascii: e", 'e', cell_at(t, 1, 0)->ch);
	ASSERT_EQ("ascii: l1", 'l', cell_at(t, 2, 0)->ch);
	ASSERT_EQ("ascii: l2", 'l', cell_at(t, 3, 0)->ch);
	ASSERT_EQ("ascii: o", 'o', cell_at(t, 4, 0)->ch);
	ASSERT_EQ("ascii: cursor_x", 5, t->cx);
	ASSERT_EQ("ascii: cursor_y", 0, t->cy);
	TEST_PASS("ascii text placement");
	test_term_free(t);
}

/* --- Newline and carriage return --- */
static void
test_newline_cr(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "AB\r\nCD");

	ASSERT_EQ("nl: A", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("nl: B", 'B', cell_at(t, 1, 0)->ch);
	ASSERT_EQ("nl: C", 'C', cell_at(t, 0, 1)->ch);
	ASSERT_EQ("nl: D", 'D', cell_at(t, 1, 1)->ch);
	ASSERT_EQ("nl: cursor_x", 2, t->cx);
	ASSERT_EQ("nl: cursor_y", 1, t->cy);
	TEST_PASS("newline and carriage return");
	test_term_free(t);
}

/* --- Line wrapping --- */
static void
test_line_wrap(void)
{
	struct term *t = test_term_new(8, 4);
	test_feed_str(t, "ABCDEFGHIJ");

	ASSERT_EQ("wrap: H at col 7", 'H', cell_at(t, 7, 0)->ch);
	ASSERT_EQ("wrap: I at col 0 row 1", 'I', cell_at(t, 0, 1)->ch);
	ASSERT_EQ("wrap: J at col 1 row 1", 'J', cell_at(t, 1, 1)->ch);
	ASSERT_EQ("wrap: cursor_x", 2, t->cx);
	ASSERT_EQ("wrap: cursor_y", 1, t->cy);
	TEST_PASS("line wrapping");
	test_term_free(t);
}

/* --- Scroll up when writing past bottom --- */
static void
test_scroll_up(void)
{
	struct term *t = test_term_new(10, 3);
	/* \n is LF only (no CR), so use \r\n to start at column 0. */
	test_feed_str(t, "AAA\r\nBBB\r\nCCC\r\nDDD");

	/* Row 0 should now have "BBB" (scrolled up). */
	ASSERT_EQ("scroll: row0 col0", 'B', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("scroll: row1 col0", 'C', cell_at(t, 0, 1)->ch);
	ASSERT_EQ("scroll: row2 col0", 'D', cell_at(t, 0, 2)->ch);
	TEST_PASS("scroll up on overflow");
	test_term_free(t);
}

/* --- CSI cursor forward (CUF) --- */
static void
test_csi_cursor_forward(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "A\033[3CB");

	ASSERT_EQ("cuf: A at 0", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("cuf: B at 4", 'B', cell_at(t, 4, 0)->ch);
	ASSERT_EQ("cuf: cursor_x", 5, t->cx);
	TEST_PASS("CSI cursor forward (CUF)");
	test_term_free(t);
}

/* --- CSI cursor up/down/back --- */
static void
test_csi_cursor_movement(void)
{
	struct term *t = test_term_new(80, 24);

	/* Move to (5, 5) using CUP, then test relative movements. */
	test_feed_str(t, "\033[6;6H");  /* CUP row 6, col 6 (1-indexed) */
	ASSERT_EQ("cup: x", 5, t->cx);
	ASSERT_EQ("cup: y", 5, t->cy);

	test_feed_str(t, "\033[2A");  /* CUU: up 2 */
	ASSERT_EQ("cuu: y", 3, t->cy);

	test_feed_str(t, "\033[3B");  /* CUD: down 3 */
	ASSERT_EQ("cud: y", 6, t->cy);

	test_feed_str(t, "\033[2D");  /* CUB: back 2 */
	ASSERT_EQ("cub: x", 3, t->cx);

	TEST_PASS("CSI cursor up/down/back");
	test_term_free(t);
}

/* --- CSI CUP (cursor position) --- */
static void
test_csi_cup(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[5;10H*");

	ASSERT_EQ("cup: star at (9,4)", '*', cell_at(t, 9, 4)->ch);
	TEST_PASS("CSI CUP (cursor position)");
	test_term_free(t);
}

/* --- SGR 8-color --- */
static void
test_sgr_basic_color(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[31mR");

	ASSERT_EQ("sgr31: fg red", (long long)palette256[1],
	    (long long)cell_at(t, 0, 0)->fg);
	TEST_PASS("SGR basic 8-color (red)");
	test_term_free(t);
}

/* --- SGR 256-color --- */
static void
test_sgr_256color(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[38;5;196mX");

	ASSERT_EQ("sgr256: fg 196", (long long)palette256[196],
	    (long long)cell_at(t, 0, 0)->fg);
	TEST_PASS("SGR 256-color");
	test_term_free(t);
}

/* --- SGR 24-bit RGB --- */
static void
test_sgr_rgb(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[38;2;255;128;0mX");

	ASSERT_EQ("sgr-rgb: fg", 0xFF8000LL, (long long)cell_at(t, 0, 0)->fg);
	TEST_PASS("SGR 24-bit RGB");
	test_term_free(t);
}

/* --- SGR background colors --- */
static void
test_sgr_background(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[41mX");

	ASSERT_EQ("sgr41: bg red", (long long)palette256[1],
	    (long long)cell_at(t, 0, 0)->bg);
	TEST_PASS("SGR background color");
	test_term_free(t);
}

/* --- SGR attributes --- */
static void
test_sgr_attributes(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[1mB\033[0m\033[2mD\033[0m\033[4mU\033[0m\033[7mR");

	ASSERT_TRUE("attr: bright", cell_at(t, 0, 0)->attr & ATTR_BRIGHT);
	ASSERT_TRUE("attr: dim", cell_at(t, 1, 0)->attr & ATTR_DIM);
	ASSERT_TRUE("attr: underline", cell_at(t, 2, 0)->attr & ATTR_UNDERLINE);
	ASSERT_TRUE("attr: reverse", cell_at(t, 3, 0)->attr & ATTR_REVERSE);
	TEST_PASS("SGR attributes (bright, dim, underline, reverse)");
	test_term_free(t);
}

/* --- SGR reset --- */
static void
test_sgr_reset(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[1;31mA\033[0mB");

	ASSERT_TRUE("reset: A is bright", cell_at(t, 0, 0)->attr & ATTR_BRIGHT);
	ASSERT_EQ("reset: B attr", 0, cell_at(t, 1, 0)->attr);
	ASSERT_EQ("reset: B fg", 0xAAAAAA, (long long)cell_at(t, 1, 0)->fg);
	TEST_PASS("SGR reset");
	test_term_free(t);
}

/* --- Erase in display (ED) --- */
static void
test_erase_display(void)
{
	struct term *t = test_term_new(10, 3);
	test_feed_str(t, "AAAAAAAAAA");
	test_feed_str(t, "BBBBBBBBBB");

	/* CSI 2J — erase all. */
	test_feed_str(t, "\033[2J");
	ASSERT_EQ("ed: cell (0,0)", ' ', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("ed: cell (5,1)", ' ', cell_at(t, 5, 1)->ch);
	TEST_PASS("erase in display (ED 2J)");
	test_term_free(t);
}

/* --- Erase in line (EL) --- */
static void
test_erase_line(void)
{
	struct term *t = test_term_new(10, 3);
	test_feed_str(t, "ABCDEFGHIJ");

	/* Move cursor to col 3, then EL 0 (erase to end of line). */
	test_feed_str(t, "\033[1;4H"); /* row 1, col 4 (1-indexed) */
	test_feed_str(t, "\033[K");

	ASSERT_EQ("el: col 0 preserved", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("el: col 2 preserved", 'C', cell_at(t, 2, 0)->ch);
	ASSERT_EQ("el: col 3 erased", ' ', cell_at(t, 3, 0)->ch);
	ASSERT_EQ("el: col 9 erased", ' ', cell_at(t, 9, 0)->ch);
	TEST_PASS("erase in line (EL)");
	test_term_free(t);
}

/* --- Tab stops --- */
static void
test_tab_stop(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "A\tB");

	ASSERT_EQ("tab: A at 0", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("tab: B at 8", 'B', cell_at(t, 8, 0)->ch);
	TEST_PASS("tab stops");
	test_term_free(t);
}

/* --- Backspace --- */
static void
test_backspace(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "AB\bC");

	ASSERT_EQ("bs: col 0", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("bs: col 1 overwritten", 'C', cell_at(t, 1, 0)->ch);
	TEST_PASS("backspace");
	test_term_free(t);
}

/* --- UTF-8 multibyte (Latin-1) --- */
static void
test_utf8_latin1(void)
{
	struct term *t = test_term_new(80, 24);
	/* Feed U+00E9 (é): 0xC3 0xA9 */
	unsigned char e_acute[] = { 0xC3, 0xA9 };
	test_feed_bytes(t, e_acute, 2);

	ASSERT_EQ("utf8: é codepoint", 0x00E9, (long long)cell_at(t, 0, 0)->ch);
	TEST_PASS("UTF-8 multibyte (Latin-1 e-acute)");
	test_term_free(t);
}

/* --- UTF-8 wide character (CJK) --- */
static void
test_utf8_wide(void)
{
	struct term *t = test_term_new(80, 24);
	/* Feed U+4E16 (世): 0xE4 0xB8 0x96 */
	unsigned char cjk[] = { 0xE4, 0xB8, 0x96 };
	test_feed_bytes(t, cjk, 3);

	ASSERT_EQ("wide: codepoint", 0x4E16, (long long)cell_at(t, 0, 0)->ch);
	ASSERT_EQ("wide: width=2", 2, cell_at(t, 0, 0)->width);
	ASSERT_EQ("wide: continuation", 0, cell_at(t, 1, 0)->width);
	ASSERT_EQ("wide: cursor_x", 2, t->cx);
	TEST_PASS("UTF-8 wide character (CJK)");
	test_term_free(t);
}

/* --- Alternate screen buffer --- */
static void
test_alt_screen(void)
{
	struct term *t = test_term_new(10, 3);
	/* Allocate alt cells (normally done in CSI handler). */
	t->alt_cells = calloc(t->cols * t->rows, sizeof(struct cell));
	for (int i = 0; i < t->cols * t->rows; i++) {
		t->alt_cells[i].ch = ' ';
		t->alt_cells[i].fg = t->fg;
		t->alt_cells[i].bg = t->bg;
		t->alt_cells[i].width = 1;
	}

	test_feed_str(t, "ORIGINAL");

	/* Enter alt screen. */
	test_feed_str(t, "\033[?1049h");
	ASSERT_TRUE("alt: using_alt", t->using_alt);

	test_feed_str(t, "ALT");
	ASSERT_EQ("alt: A on alt", 'A', cell_at(t, 0, 0)->ch);

	/* Leave alt screen. */
	test_feed_str(t, "\033[?1049l");
	ASSERT_TRUE("alt: returned", !t->using_alt);
	ASSERT_EQ("alt: original O", 'O', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("alt: original R", 'R', cell_at(t, 1, 0)->ch);

	TEST_PASS("alternate screen buffer");
	test_term_free(t);
}

/* --- OSC ignored gracefully --- */
static void
test_osc_ignored(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033]0;window title\007X");

	ASSERT_EQ("osc: X placed after OSC", 'X', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("osc: parser back to ground", S_GROUND, (int)t->state);
	TEST_PASS("OSC sequences ignored gracefully");
	test_term_free(t);
}

/* --- screen_contains_text --- */
static void
test_screen_contains(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "Hello World");

	ASSERT_TRUE("contains: Hello", screen_contains_text(t, "Hello"));
	ASSERT_TRUE("contains: World", screen_contains_text(t, "World"));
	ASSERT_TRUE("contains: not found", !screen_contains_text(t, "foobar"));
	TEST_PASS("screen_contains_text");
	test_term_free(t);
}

/* --- Bright colors (90-97) --- */
static void
test_sgr_bright_colors(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[91mX");

	ASSERT_EQ("sgr91: bright red", (long long)palette256[9],
	    (long long)cell_at(t, 0, 0)->fg);
	TEST_PASS("SGR bright colors (90-97)");
	test_term_free(t);
}

/* --- Default fg/bg restore (SGR 39/49) --- */
static void
test_sgr_default_restore(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[31m\033[39mX");

	ASSERT_EQ("sgr39: default fg", 0xAAAAAA, (long long)cell_at(t, 0, 0)->fg);

	test_feed_str(t, "\033[41m\033[49mY");
	ASSERT_EQ("sgr49: default bg", 0x0C0C16LL, (long long)cell_at(t, 1, 0)->bg);

	TEST_PASS("SGR 39/49 default color restore");
	test_term_free(t);
}

/* --- VPA (vertical position absolute) --- */
static void
test_vpa(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[10d");

	ASSERT_EQ("vpa: cursor_y", 9, t->cy);
	TEST_PASS("VPA (vertical position absolute)");
	test_term_free(t);
}

/* --- Multiple params in one CSI --- */
static void
test_sgr_combined_params(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[1;4;31mX");

	struct cell *c = cell_at(t, 0, 0);
	ASSERT_TRUE("combined: bright", c->attr & ATTR_BRIGHT);
	ASSERT_TRUE("combined: underline", c->attr & ATTR_UNDERLINE);
	ASSERT_EQ("combined: red fg", (long long)palette256[1], (long long)c->fg);
	TEST_PASS("SGR combined params (bold+underline+red)");
	test_term_free(t);
}

/* --- ED mode 0 (erase below) --- */
static void
test_ed_below(void)
{
	struct term *t = test_term_new(10, 3);
	test_feed_str(t, "ROW0LINE..");
	test_feed_str(t, "ROW1LINE..");
	test_feed_str(t, "ROW2LINE..");

	/* Cursor to row 1, col 5, then ED 0 (erase below). */
	test_feed_str(t, "\033[2;6H");
	test_feed_str(t, "\033[0J");

	/* Row 0 should be untouched. */
	ASSERT_EQ("ed0: row0 preserved", 'R', cell_at(t, 0, 0)->ch);
	/* Row 1 cols 0-4 should be preserved, 5+ cleared. */
	ASSERT_EQ("ed0: row1 col4 preserved", 'L', cell_at(t, 4, 1)->ch);
	ASSERT_EQ("ed0: row1 col5 cleared", ' ', cell_at(t, 5, 1)->ch);
	/* Row 2 should be cleared. */
	ASSERT_EQ("ed0: row2 cleared", ' ', cell_at(t, 0, 2)->ch);
	TEST_PASS("ED mode 0 (erase below)");
	test_term_free(t);
}

/* --- ICH — insert characters --- */
static void
test_ich(void)
{
	struct term *t = test_term_new(10, 3);
	test_feed_str(t, "ABCDEF");
	/* Move to col 2, insert 2 chars. */
	test_feed_str(t, "\033[1;3H"); /* col 3 (1-indexed) = col 2 */
	test_feed_str(t, "\033[2@");

	ASSERT_EQ("ich: col0 A", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("ich: col1 B", 'B', cell_at(t, 1, 0)->ch);
	ASSERT_EQ("ich: col2 space", ' ', cell_at(t, 2, 0)->ch);
	ASSERT_EQ("ich: col3 space", ' ', cell_at(t, 3, 0)->ch);
	ASSERT_EQ("ich: col4 C", 'C', cell_at(t, 4, 0)->ch);
	ASSERT_EQ("ich: col5 D", 'D', cell_at(t, 5, 0)->ch);
	/* E and F pushed off the right if cols=10, they'd be at 6,7. */
	ASSERT_EQ("ich: col6 E", 'E', cell_at(t, 6, 0)->ch);
	ASSERT_EQ("ich: col7 F", 'F', cell_at(t, 7, 0)->ch);
	TEST_PASS("ICH: insert characters");
	test_term_free(t);
}

/* --- DCH — delete characters --- */
static void
test_dch(void)
{
	struct term *t = test_term_new(10, 3);
	test_feed_str(t, "ABCDEFGHIJ");
	/* Move to col 2, delete 2 chars. */
	test_feed_str(t, "\033[1;3H");
	test_feed_str(t, "\033[2P");

	ASSERT_EQ("dch: col0 A", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("dch: col1 B", 'B', cell_at(t, 1, 0)->ch);
	ASSERT_EQ("dch: col2 E", 'E', cell_at(t, 2, 0)->ch);
	ASSERT_EQ("dch: col3 F", 'F', cell_at(t, 3, 0)->ch);
	ASSERT_EQ("dch: col7 J", 'J', cell_at(t, 7, 0)->ch);
	/* Last 2 cols should be blank. */
	ASSERT_EQ("dch: col8 space", ' ', cell_at(t, 8, 0)->ch);
	ASSERT_EQ("dch: col9 space", ' ', cell_at(t, 9, 0)->ch);
	TEST_PASS("DCH: delete characters");
	test_term_free(t);
}

/* --- IL — insert lines --- */
static void
test_il(void)
{
	struct term *t = test_term_new(10, 5);
	test_feed_str(t, "\033[1;1HAAAA\033[2;1HBBBB\033[3;1HCCCC\033[4;1HDDDD\033[5;1HEEEE");
	/* Move to row 2 (0-indexed: 1), insert 1 line. */
	test_feed_str(t, "\033[2;1H");
	test_feed_str(t, "\033[1L");

	ASSERT_EQ("il: row0 A", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("il: row1 blank", ' ', cell_at(t, 0, 1)->ch);
	ASSERT_EQ("il: row2 B", 'B', cell_at(t, 0, 2)->ch);
	ASSERT_EQ("il: row3 C", 'C', cell_at(t, 0, 3)->ch);
	/* Row 4 should have D (E was pushed off bottom). */
	ASSERT_EQ("il: row4 D", 'D', cell_at(t, 0, 4)->ch);
	TEST_PASS("IL: insert lines");
	test_term_free(t);
}

/* --- DL — delete lines --- */
static void
test_dl(void)
{
	struct term *t = test_term_new(10, 5);
	test_feed_str(t, "\033[1;1HAAAA\033[2;1HBBBB\033[3;1HCCCC\033[4;1HDDDD\033[5;1HEEEE");
	/* Move to row 2, delete 1 line. */
	test_feed_str(t, "\033[2;1H");
	test_feed_str(t, "\033[1M");

	ASSERT_EQ("dl: row0 A", 'A', cell_at(t, 0, 0)->ch);
	ASSERT_EQ("dl: row1 C", 'C', cell_at(t, 0, 1)->ch);
	ASSERT_EQ("dl: row2 D", 'D', cell_at(t, 0, 2)->ch);
	ASSERT_EQ("dl: row3 E", 'E', cell_at(t, 0, 3)->ch);
	ASSERT_EQ("dl: row4 blank", ' ', cell_at(t, 0, 4)->ch);
	TEST_PASS("DL: delete lines");
	test_term_free(t);
}

/* --- SGR 3 italic and SGR 23 disable italic --- */
static void
test_sgr_italic(void)
{
	struct term *t = test_term_new(80, 24);
	test_feed_str(t, "\033[3mI\033[23mN");

	ASSERT_TRUE("italic: I has ATTR_ITALIC", cell_at(t, 0, 0)->attr & ATTR_ITALIC);
	ASSERT_TRUE("italic: N not italic", !(cell_at(t, 1, 0)->attr & ATTR_ITALIC));
	TEST_PASS("SGR 3/23: italic set and unset");
	test_term_free(t);
}

/* --- SGR 22/24/27 individual resets --- */
static void
test_sgr_individual_resets(void)
{
	struct term *t = test_term_new(80, 24);
	/* Bold + underline + reverse, then reset each individually. */
	test_feed_str(t, "\033[1;4;7mX");
	ASSERT_TRUE("indiv: X has bright", cell_at(t, 0, 0)->attr & ATTR_BRIGHT);
	ASSERT_TRUE("indiv: X has underline", cell_at(t, 0, 0)->attr & ATTR_UNDERLINE);
	ASSERT_TRUE("indiv: X has reverse", cell_at(t, 0, 0)->attr & ATTR_REVERSE);

	test_feed_str(t, "\033[22mA"); /* disable bold/dim */
	ASSERT_TRUE("indiv: A no bright", !(cell_at(t, 1, 0)->attr & ATTR_BRIGHT));
	ASSERT_TRUE("indiv: A still underline", cell_at(t, 1, 0)->attr & ATTR_UNDERLINE);

	test_feed_str(t, "\033[24mB"); /* disable underline */
	ASSERT_TRUE("indiv: B no underline", !(cell_at(t, 2, 0)->attr & ATTR_UNDERLINE));
	ASSERT_TRUE("indiv: B still reverse", cell_at(t, 2, 0)->attr & ATTR_REVERSE);

	test_feed_str(t, "\033[27mC"); /* disable reverse */
	ASSERT_TRUE("indiv: C no reverse", !(cell_at(t, 3, 0)->attr & ATTR_REVERSE));
	ASSERT_EQ("indiv: C attr=0", 0, cell_at(t, 3, 0)->attr);

	TEST_PASS("SGR 22/24/27: individual attribute resets");
	test_term_free(t);
}

/* --- DECTCEM cursor visibility --- */
static void
test_dectcem(void)
{
	struct term *t = test_term_new(80, 24);
	ASSERT_EQ("dectcem: default visible", 1, t->cursor_visible);

	/* Hide cursor: CSI ? 25 l */
	test_feed_str(t, "\033[?25l");
	ASSERT_EQ("dectcem: hidden", 0, t->cursor_visible);

	/* Show cursor: CSI ? 25 h */
	test_feed_str(t, "\033[?25h");
	ASSERT_EQ("dectcem: shown", 1, t->cursor_visible);

	TEST_PASS("DECTCEM: cursor visibility mode 25");
	test_term_free(t);
}

/* --- DECSC/DECRC — save/restore cursor --- */
static void
test_decsc_decrc(void)
{
	struct term *t = test_term_new(80, 24);

	/* Set position and attributes, then save. */
	test_feed_str(t, "\033[5;10H\033[1;31m");
	test_feed_str(t, "\0337"); /* DECSC — ESC 7 */

	ASSERT_EQ("decsc: saved cx", 9, t->saved_cx);
	ASSERT_EQ("decsc: saved cy", 4, t->saved_cy);
	ASSERT_TRUE("decsc: cursor_saved", t->cursor_saved);

	/* Move somewhere else and change attributes. */
	test_feed_str(t, "\033[1;1H\033[0m");
	ASSERT_EQ("decsc: moved cx", 0, t->cx);
	ASSERT_EQ("decsc: moved cy", 0, t->cy);

	/* Restore. */
	test_feed_str(t, "\0338"); /* DECRC — ESC 8 */
	ASSERT_EQ("decrc: restored cx", 9, t->cx);
	ASSERT_EQ("decrc: restored cy", 4, t->cy);
	ASSERT_TRUE("decrc: restored bright", t->attr & ATTR_BRIGHT);
	ASSERT_EQ("decrc: restored fg red", (long long)palette256[1], (long long)t->fg);

	TEST_PASS("DECSC/DECRC: save and restore cursor + attributes");
	test_term_free(t);
}

/* --- DECSTBM basic scroll region --- */
static void
test_decstbm_basic(void)
{
	struct term *t = test_term_new(10, 5);

	/* Set scroll region to rows 2-4 (1-indexed). */
	test_feed_str(t, "\033[2;4r");
	ASSERT_EQ("stbm: scroll_top", 1, t->scroll_top);
	ASSERT_EQ("stbm: scroll_bottom", 3, t->scroll_bottom);
	/* DECSTBM resets cursor to home. */
	ASSERT_EQ("stbm: cx=0", 0, t->cx);
	ASSERT_EQ("stbm: cy=0", 0, t->cy);
	TEST_PASS("DECSTBM basic: sets scroll region");
	test_term_free(t);
}

/* --- Scroll within region --- */
static void
test_decstbm_scroll(void)
{
	struct term *t = test_term_new(10, 5);

	/* Write content on every row. */
	test_feed_str(t, "\033[1;1HA\033[2;1HB\033[3;1HC\033[4;1HD\033[5;1HE");

	/* Set scroll region rows 2-4 (0-indexed: 1-3). */
	test_feed_str(t, "\033[2;4r");

	/* Move cursor to bottom of scroll region and write a newline. */
	test_feed_str(t, "\033[4;1H");
	test_feed_str(t, "\n");

	/* Row 0 (A) should be untouched — outside scroll region. */
	ASSERT_EQ("scroll-rgn: row0", 'A', cell_at(t, 0, 0)->ch);
	/* Row 1 should now have 'C' (was row 2, scrolled up). */
	ASSERT_EQ("scroll-rgn: row1", 'C', cell_at(t, 0, 1)->ch);
	/* Row 2 should now have 'D' (was row 3, scrolled up). */
	ASSERT_EQ("scroll-rgn: row2", 'D', cell_at(t, 0, 2)->ch);
	/* Row 3 should be cleared (new line at bottom of region). */
	ASSERT_EQ("scroll-rgn: row3 cleared", ' ', cell_at(t, 0, 3)->ch);
	/* Row 4 (E) should be untouched — outside scroll region. */
	ASSERT_EQ("scroll-rgn: row4", 'E', cell_at(t, 0, 4)->ch);
	TEST_PASS("DECSTBM: scroll within region preserves outside rows");
	test_term_free(t);
}

/* --- Reset scroll region --- */
static void
test_decstbm_reset(void)
{
	struct term *t = test_term_new(10, 5);
	test_feed_str(t, "\033[2;4r");
	ASSERT_EQ("reset-pre: top", 1, t->scroll_top);

	/* Reset scroll region. */
	test_feed_str(t, "\033[r");
	ASSERT_EQ("reset: top", 0, t->scroll_top);
	ASSERT_EQ("reset: bottom", -1, t->scroll_bottom);
	TEST_PASS("DECSTBM: reset to full screen");
	test_term_free(t);
}

/* --- Header/footer pattern (fixed rows) --- */
static void
test_decstbm_header_footer(void)
{
	struct term *t = test_term_new(10, 6);

	/* Row 0 = header, row 5 = footer. Scroll region = rows 2-5 (1-indexed). */
	test_feed_str(t, "\033[1;1HHDR");
	test_feed_str(t, "\033[6;1HFTR");

	/* Set scroll region to rows 2-5 (middle area). */
	test_feed_str(t, "\033[2;5r");

	/* Fill scroll region. */
	test_feed_str(t, "\033[2;1H1111111111");
	test_feed_str(t, "\033[3;1H2222222222");
	test_feed_str(t, "\033[4;1H3333333333");
	test_feed_str(t, "\033[5;1H4444444444");

	/* Scroll once by LF at bottom of region. */
	test_feed_str(t, "\033[5;1H");
	test_feed_str(t, "\n");

	/* Header untouched. */
	ASSERT_EQ("hdr/ftr: header H", 'H', cell_at(t, 0, 0)->ch);
	/* Footer untouched. */
	ASSERT_EQ("hdr/ftr: footer F", 'F', cell_at(t, 0, 5)->ch);
	/* Row 1 should have '2' (scrolled up from row 2). */
	ASSERT_EQ("hdr/ftr: row1", '2', cell_at(t, 0, 1)->ch);
	/* Row 4 should be cleared. */
	ASSERT_EQ("hdr/ftr: row4 cleared", ' ', cell_at(t, 0, 4)->ch);
	TEST_PASS("DECSTBM: header/footer preserved during scroll");
	test_term_free(t);
}

/* --- DA1 response --- */
static void
test_da1_response(void)
{
	struct term *t = test_term_new(80, 24);

	/* Set up a pipe so we can read the response. */
	int pipefd[2];
	if (pipe(pipefd) == 0) {
		t->pty_fd = pipefd[1];
		test_feed_str(t, "\033[c");

		/* Read the response. */
		char buf[64] = {0};
		/* Set non-blocking so we don't hang. */
		fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
		ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
		close(pipefd[0]);
		close(pipefd[1]);
		t->pty_fd = -1;

		ASSERT_TRUE("da1: got response", n > 0);
		/* Should contain SIXEL flag (4). */
		ASSERT_TRUE("da1: contains VT220+SIXEL",
		    strstr(buf, "\033[?62;4;22c") != NULL);
		TEST_PASS("DA1 response (VT220 + SIXEL)");
	} else {
		TEST_FAIL("DA1 response", "pipe() failed");
	}
	test_term_free(t);
}

int
main(void)
{
	printf("=== VT Parser Tests ===\n");

	RUN_TEST(test_ascii_text);
	RUN_TEST(test_newline_cr);
	RUN_TEST(test_line_wrap);
	RUN_TEST(test_scroll_up);
	RUN_TEST(test_csi_cursor_forward);
	RUN_TEST(test_csi_cursor_movement);
	RUN_TEST(test_csi_cup);
	RUN_TEST(test_sgr_basic_color);
	RUN_TEST(test_sgr_256color);
	RUN_TEST(test_sgr_rgb);
	RUN_TEST(test_sgr_background);
	RUN_TEST(test_sgr_attributes);
	RUN_TEST(test_sgr_reset);
	RUN_TEST(test_sgr_bright_colors);
	RUN_TEST(test_sgr_default_restore);
	RUN_TEST(test_sgr_combined_params);
	RUN_TEST(test_erase_display);
	RUN_TEST(test_erase_line);
	RUN_TEST(test_ed_below);
	RUN_TEST(test_tab_stop);
	RUN_TEST(test_backspace);
	RUN_TEST(test_utf8_latin1);
	RUN_TEST(test_utf8_wide);
	RUN_TEST(test_alt_screen);
	RUN_TEST(test_osc_ignored);
	RUN_TEST(test_screen_contains);
	RUN_TEST(test_vpa);
	RUN_TEST(test_ich);
	RUN_TEST(test_dch);
	RUN_TEST(test_il);
	RUN_TEST(test_dl);
	RUN_TEST(test_dectcem);
	RUN_TEST(test_sgr_italic);
	RUN_TEST(test_sgr_individual_resets);
	RUN_TEST(test_decsc_decrc);
	RUN_TEST(test_decstbm_basic);
	RUN_TEST(test_decstbm_scroll);
	RUN_TEST(test_decstbm_reset);
	RUN_TEST(test_decstbm_header_footer);
	RUN_TEST(test_da1_response);

	TEST_SUMMARY();
}
