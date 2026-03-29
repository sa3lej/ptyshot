/*
 * test.h — Minimal test framework for ptyshot.
 * No dependencies. Just macros and a runner.
 */

#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int test_pass_count;
static int test_fail_count;

#define TEST_PASS(name) do { \
	test_pass_count++; \
	printf("  PASS  %s\n", name); \
} while (0)

#define TEST_FAIL(name, msg) do { \
	test_fail_count++; \
	printf("  FAIL  %s — %s (at %s:%d)\n", name, msg, __FILE__, __LINE__); \
} while (0)

#define ASSERT_EQ(name, expected, actual) do { \
	long long _e = (long long)(expected), _a = (long long)(actual); \
	if (_e != _a) { \
		char _buf[256]; \
		snprintf(_buf, sizeof _buf, "expected %lld, got %lld", _e, _a); \
		TEST_FAIL(name, _buf); \
		return; \
	} \
} while (0)

#define ASSERT_TRUE(name, expr) do { \
	if (!(expr)) { \
		TEST_FAIL(name, "assertion failed: " #expr); \
		return; \
	} \
} while (0)

#define ASSERT_STR_EQ(name, expected, actual) do { \
	if (strcmp((expected), (actual)) != 0) { \
		char _buf[512]; \
		snprintf(_buf, sizeof _buf, "expected \"%s\", got \"%s\"", \
		    (expected), (actual)); \
		TEST_FAIL(name, _buf); \
		return; \
	} \
} while (0)

#define ASSERT_MEM_EQ(name, expected, actual, len) do { \
	if (memcmp((expected), (actual), (len)) != 0) { \
		TEST_FAIL(name, "memory mismatch"); \
		return; \
	} \
} while (0)

/* Helper to allocate and initialize a term for testing. */
static struct term *
test_term_new(int cols, int rows)
{
	init_palette();
	struct term *t = calloc(1, sizeof(struct term));
	t->cols = cols;
	t->rows = rows;
	t->fg = 0xAAAAAA;
	t->bg = 0x0C0C16;
	t->attr = 0;
	t->pty_fd = -1; /* no real PTY */
	t->scroll_bottom = -1;
	t->cursor_visible = 1;
	t->cells = calloc(cols * rows, sizeof(struct cell));
	for (int i = 0; i < cols * rows; i++) {
		t->cells[i].ch = ' ';
		t->cells[i].fg = t->fg;
		t->cells[i].bg = t->bg;
		t->cells[i].width = 1;
	}
	return t;
}

static void
test_term_free(struct term *t)
{
	free(t->cells);
	free(t->alt_cells);
	free(t->sixel_mask);
	free(t->dcs_buf);
	for (int i = 0; i < t->nimages; i++)
		free(t->images[i].pixels);
	free(t);
}

/* Feed a string to the terminal. */
static void
test_feed_str(struct term *t, const char *s)
{
	while (*s)
		term_feed(t, (unsigned char)*s++);
}

/* Feed raw bytes (for binary/UTF-8 sequences). */
static void
test_feed_bytes(struct term *t, const unsigned char *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
		term_feed(t, data[i]);
}

#define RUN_TEST(fn) do { \
	fn(); \
} while (0)

#define TEST_SUMMARY() do { \
	printf("\n%d passed, %d failed, %d total\n", \
	    test_pass_count, test_fail_count, \
	    test_pass_count + test_fail_count); \
	return test_fail_count > 0 ? 1 : 0; \
} while (0)

#endif /* TEST_H */
