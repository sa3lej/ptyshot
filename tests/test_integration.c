/*
 * test_integration.c — Integration tests that run the ptyshot binary
 * and verify output content, not just file existence.
 *
 * Uses stb_image to decode PNGs and compare pixel data.
 * Does NOT include ptyshot.c — tests the compiled binary only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"

/* Minimal test framework (inline since we don't include ptyshot.c). */
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

#define RUN_TEST(fn) do { fn(); } while (0)

#define TEST_SUMMARY() do { \
	printf("\n%d passed, %d failed, %d total\n", \
	    test_pass_count, test_fail_count, \
	    test_pass_count + test_fail_count); \
	return test_fail_count > 0 ? 1 : 0; \
} while (0)

/* Path to the ptyshot binary (relative to test execution dir). */
#define PTYSHOT "./ptyshot"

/* Run a command and return its exit code. */
static int
run_cmd(const char *cmd)
{
	int rc = system(cmd);
	if (rc == -1) return -1;
	return WEXITSTATUS(rc);
}

/* Run a command and capture stdout into a buffer. Returns bytes read. */
static ssize_t
run_capture(const char *cmd, char *buf, size_t bufsz)
{
	FILE *fp = popen(cmd, "r");
	if (!fp) return -1;
	size_t total = 0;
	size_t n;
	while ((n = fread(buf + total, 1, bufsz - total - 1, fp)) > 0)
		total += n;
	buf[total] = '\0';
	pclose(fp);
	return (ssize_t)total;
}

/* Check if a file exists and is non-empty. */
static int
file_exists_nonempty(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	return st.st_size > 0;
}

/* --- PNG dimensions are correct --- */
static void
test_png_dimensions(void)
{
	const char *out = "/tmp/ptyshot-test-dims.png";
	char cmd[512];
	snprintf(cmd, sizeof cmd,
	    "%s -o %s -w 200 80x24 bash -c 'echo x' 2>/dev/null", PTYSHOT, out);
	run_cmd(cmd);

	int w, h, ch;
	unsigned char *px = stbi_load(out, &w, &h, &ch, 4);
	ASSERT_TRUE("dims: PNG loaded", px != NULL);
	ASSERT_EQ("dims: width=640", 640, w);   /* 80 * 8 */
	ASSERT_EQ("dims: height=384", 384, h);  /* 24 * 16 */

	TEST_PASS("PNG dimensions 80x24 -> 640x384");
	stbi_image_free(px);
	unlink(out);
}

/* --- Smaller terminal size --- */
static void
test_png_small_terminal(void)
{
	const char *out = "/tmp/ptyshot-test-small.png";
	char cmd[512];
	snprintf(cmd, sizeof cmd,
	    "%s -o %s -w 200 40x10 bash -c 'echo x' 2>/dev/null", PTYSHOT, out);
	run_cmd(cmd);

	int w, h, ch;
	unsigned char *px = stbi_load(out, &w, &h, &ch, 4);
	ASSERT_TRUE("small: PNG loaded", px != NULL);
	ASSERT_EQ("small: width=320", 320, w);   /* 40 * 8 */
	ASSERT_EQ("small: height=160", 160, h);  /* 10 * 16 */

	TEST_PASS("PNG dimensions 40x10 -> 320x160");
	stbi_image_free(px);
	unlink(out);
}

/* --- --text output contains expected text --- */
static void
test_text_output_content(void)
{
	char buf[4096];
	ssize_t n = run_capture(
	    PTYSHOT " --text -w 200 80x24 bash -c 'printf \"MARKER_XYZ\\n\"' 2>/dev/null",
	    buf, sizeof buf);
	ASSERT_TRUE("text: got output", n > 0);
	ASSERT_TRUE("text: contains MARKER_XYZ", strstr(buf, "MARKER_XYZ") != NULL);

	TEST_PASS("--text output contains expected text");
}

/* --- --json output has correct structure --- */
static void
test_json_output(void)
{
	char buf[65536];
	ssize_t n = run_capture(
	    PTYSHOT " --json -w 200 10x3 bash -c 'printf AB' 2>/dev/null",
	    buf, sizeof buf);
	ASSERT_TRUE("json: got output", n > 0);
	/* Check for cols/rows fields. */
	ASSERT_TRUE("json: has cols", strstr(buf, "\"cols\":10") != NULL ||
	    strstr(buf, "\"cols\": 10") != NULL);
	ASSERT_TRUE("json: has rows", strstr(buf, "\"rows\":3") != NULL ||
	    strstr(buf, "\"rows\": 3") != NULL);
	/* Check that cell data includes 'A'. */
	ASSERT_TRUE("json: contains A", strstr(buf, "\"A\"") != NULL);

	TEST_PASS("--json output structure and content");
}

/* --- --base64 output is valid PNG --- */
static void
test_base64_output(void)
{
	char buf[131072];
	ssize_t n = run_capture(
	    PTYSHOT " --base64 -w 200 80x24 bash -c 'echo test' 2>/dev/null",
	    buf, sizeof buf);
	ASSERT_TRUE("b64: got output", n > 0);
	/* Base64 of PNG starts with iVBORw0KGgo (the PNG magic bytes). */
	ASSERT_TRUE("b64: starts with PNG magic",
	    strncmp(buf, "iVBORw0KGgo", 11) == 0);

	TEST_PASS("--base64 output starts with PNG magic");
}

/* --- Keystroke delivery works --- */
static void
test_keystroke_delivery(void)
{
	char buf[4096];
	ssize_t n = run_capture(
	    PTYSHOT " --text -k 'enter' -w 200 80x24 bash -c 'read line; echo GOT_INPUT' 2>/dev/null",
	    buf, sizeof buf);
	ASSERT_TRUE("key: got output", n > 0);
	ASSERT_TRUE("key: contains GOT_INPUT", strstr(buf, "GOT_INPUT") != NULL);

	TEST_PASS("keystroke delivery (-k enter)");
}

/* --- Record mode creates multiple frames --- */
static void
test_record_mode(void)
{
	/* Clean up first. */
	unlink("/tmp/ptyshot-rec-int_000.png");
	unlink("/tmp/ptyshot-rec-int_001.png");
	unlink("/tmp/ptyshot-rec-int_002.png");
	unlink("/tmp/ptyshot-rec-int-final.png");

	char cmd[512];
	snprintf(cmd, sizeof cmd,
	    "%s -o /tmp/ptyshot-rec-int-final.png -w 200 "
	    "-k enter -R 3:100:/tmp/ptyshot-rec-int "
	    "80x24 bash -c 'read; echo L1; sleep 0.2; echo L2; sleep 0.2; echo L3; sleep 1' 2>/dev/null",
	    PTYSHOT);
	run_cmd(cmd);

	ASSERT_TRUE("rec: frame 0 exists", file_exists_nonempty("/tmp/ptyshot-rec-int_000.png"));
	ASSERT_TRUE("rec: frame 1 exists", file_exists_nonempty("/tmp/ptyshot-rec-int_001.png"));
	ASSERT_TRUE("rec: frame 2 exists", file_exists_nonempty("/tmp/ptyshot-rec-int_002.png"));
	ASSERT_TRUE("rec: final exists", file_exists_nonempty("/tmp/ptyshot-rec-int-final.png"));

	/* Verify frame 0 is a valid PNG with correct dimensions. */
	int w, h, ch;
	unsigned char *px = stbi_load("/tmp/ptyshot-rec-int_000.png", &w, &h, &ch, 4);
	ASSERT_TRUE("rec: frame 0 is valid PNG", px != NULL);
	ASSERT_EQ("rec: frame 0 width", 640, w);
	stbi_image_free(px);

	TEST_PASS("record mode creates 3 valid PNG frames");

	unlink("/tmp/ptyshot-rec-int_000.png");
	unlink("/tmp/ptyshot-rec-int_001.png");
	unlink("/tmp/ptyshot-rec-int_002.png");
	unlink("/tmp/ptyshot-rec-int-final.png");
}

/* --- Golden test: hello world pixel comparison --- */
static void
test_golden_hello(void)
{
	const char *out = "/tmp/ptyshot-golden-hello.png";
	const char *golden = "tests/golden/hello.png";
	char cmd[512];
	snprintf(cmd, sizeof cmd,
	    "%s -o %s -w 200 40x5 bash -c 'printf \"Hello World\\n\"' 2>/dev/null",
	    PTYSHOT, out);
	run_cmd(cmd);

	/* If golden doesn't exist yet, generate it. */
	if (!file_exists_nonempty(golden)) {
		char cp[512];
		snprintf(cp, sizeof cp, "cp -f %s %s", out, golden);
		run_cmd(cp);
		printf("  NOTE  generated golden: %s\n", golden);
	}

	int w1, h1, ch1, w2, h2, ch2;
	unsigned char *px1 = stbi_load(out, &w1, &h1, &ch1, 4);
	unsigned char *px2 = stbi_load(golden, &w2, &h2, &ch2, 4);

	ASSERT_TRUE("golden-hello: output loaded", px1 != NULL);
	ASSERT_TRUE("golden-hello: golden loaded", px2 != NULL);
	ASSERT_EQ("golden-hello: width match", w2, w1);
	ASSERT_EQ("golden-hello: height match", h2, h1);

	/* Pixel-by-pixel comparison. */
	int mismatches = 0;
	for (int i = 0; i < w1 * h1 * 4; i++) {
		if (px1[i] != px2[i]) mismatches++;
	}
	if (mismatches > 0) {
		char msg[128];
		snprintf(msg, sizeof msg, "%d pixel bytes differ", mismatches);
		TEST_FAIL("golden-hello: pixel match", msg);
	} else {
		TEST_PASS("golden: hello world pixel-perfect match");
	}

	stbi_image_free(px1);
	stbi_image_free(px2);
	unlink(out);
}

/* --- Golden test: colored text --- */
static void
test_golden_color(void)
{
	const char *out = "/tmp/ptyshot-golden-color.png";
	const char *golden = "tests/golden/color.png";
	char cmd[512];
	snprintf(cmd, sizeof cmd,
	    "%s -o %s -w 200 40x5 bash -c '"
	    "printf \"\\033[31mRED\\033[32mGREEN\\033[34mBLUE\\033[0m\\n\"'"
	    " 2>/dev/null", PTYSHOT, out);
	run_cmd(cmd);

	if (!file_exists_nonempty(golden)) {
		char cp[512];
		snprintf(cp, sizeof cp, "cp -f %s %s", out, golden);
		run_cmd(cp);
		printf("  NOTE  generated golden: %s\n", golden);
	}

	int w1, h1, ch1, w2, h2, ch2;
	unsigned char *px1 = stbi_load(out, &w1, &h1, &ch1, 4);
	unsigned char *px2 = stbi_load(golden, &w2, &h2, &ch2, 4);

	ASSERT_TRUE("golden-color: output loaded", px1 != NULL);
	ASSERT_TRUE("golden-color: golden loaded", px2 != NULL);
	ASSERT_EQ("golden-color: width match", w2, w1);
	ASSERT_EQ("golden-color: height match", h2, h1);

	int mismatches = 0;
	for (int i = 0; i < w1 * h1 * 4; i++) {
		if (px1[i] != px2[i]) mismatches++;
	}
	if (mismatches > 0) {
		char msg[128];
		snprintf(msg, sizeof msg, "%d pixel bytes differ", mismatches);
		TEST_FAIL("golden-color: pixel match", msg);
	} else {
		TEST_PASS("golden: colored text pixel-perfect match");
	}

	stbi_image_free(px1);
	stbi_image_free(px2);
	unlink(out);
}

/* --- Golden test: box drawing --- */
static void
test_golden_box(void)
{
	const char *out = "/tmp/ptyshot-golden-box.png";
	const char *golden = "tests/golden/box.png";
	char cmd[512];
	snprintf(cmd, sizeof cmd,
	    "%s -o %s -w 200 40x5 bash -c '"
	    "printf \"\\xe2\\x94\\x8c\\xe2\\x94\\x80\\xe2\\x94\\x80\\xe2\\x94\\x80\\xe2\\x94\\x90\\n\""
	    "; printf \"\\xe2\\x94\\x82 A \\xe2\\x94\\x82\\n\""
	    "; printf \"\\xe2\\x94\\x94\\xe2\\x94\\x80\\xe2\\x94\\x80\\xe2\\x94\\x80\\xe2\\x94\\x98\\n\"'"
	    " 2>/dev/null", PTYSHOT, out);
	run_cmd(cmd);

	if (!file_exists_nonempty(golden)) {
		char cp[512];
		snprintf(cp, sizeof cp, "cp -f %s %s", out, golden);
		run_cmd(cp);
		printf("  NOTE  generated golden: %s\n", golden);
	}

	int w1, h1, ch1, w2, h2, ch2;
	unsigned char *px1 = stbi_load(out, &w1, &h1, &ch1, 4);
	unsigned char *px2 = stbi_load(golden, &w2, &h2, &ch2, 4);

	ASSERT_TRUE("golden-box: output loaded", px1 != NULL);
	ASSERT_TRUE("golden-box: golden loaded", px2 != NULL);
	ASSERT_EQ("golden-box: width match", w2, w1);
	ASSERT_EQ("golden-box: height match", h2, h1);

	int mismatches = 0;
	for (int i = 0; i < w1 * h1 * 4; i++) {
		if (px1[i] != px2[i]) mismatches++;
	}
	if (mismatches > 0) {
		char msg[128];
		snprintf(msg, sizeof msg, "%d pixel bytes differ", mismatches);
		TEST_FAIL("golden-box: pixel match", msg);
	} else {
		TEST_PASS("golden: box drawing pixel-perfect match");
	}

	stbi_image_free(px1);
	stbi_image_free(px2);
	unlink(out);
}

/* --- Verify rendered text is actually visible in pixels --- */
static void
test_text_visible_in_png(void)
{
	const char *out = "/tmp/ptyshot-test-visible.png";
	char cmd[512];
	snprintf(cmd, sizeof cmd,
	    "%s -o %s -w 200 40x5 bash -c 'printf \"X\"' 2>/dev/null",
	    PTYSHOT, out);
	run_cmd(cmd);

	int w, h, ch;
	unsigned char *px = stbi_load(out, &w, &h, &ch, 4);
	ASSERT_TRUE("visible: PNG loaded", px != NULL);

	/*
	 * Cell (0,0) should have foreground pixels (from 'X' glyph).
	 * The default fg is 0xAAAAAA. Check that at least some pixels
	 * in the first cell are not the default bg (0x0C0C16).
	 */
	int fg_pixels = 0;
	for (int y = 0; y < 16; y++) {
		for (int x = 0; x < 8; x++) {
			int off = (y * w + x) * 4;
			uint8_t r = px[off], g = px[off+1], b = px[off+2];
			if (r != 0x0C || g != 0x0C || b != 0x16)
				fg_pixels++;
		}
	}
	ASSERT_TRUE("visible: 'X' has foreground pixels", fg_pixels > 0);

	/* Cell (2,0) should be all background (empty, no cursor). */
	int bg_only = 1;
	for (int y = 0; y < 16; y++) {
		for (int x = 16; x < 24; x++) {
			int off = (y * w + x) * 4;
			uint8_t r = px[off], g = px[off+1], b = px[off+2];
			if (r != 0x0C || g != 0x0C || b != 0x16)
				bg_only = 0;
		}
	}
	ASSERT_TRUE("visible: cell 2 is all background", bg_only);

	TEST_PASS("rendered text is visible in PNG pixels");
	stbi_image_free(px);
	unlink(out);
}

/* --- Color is correct in rendered PNG --- */
static void
test_color_in_png(void)
{
	const char *out = "/tmp/ptyshot-test-color.png";
	char cmd[512];
	/* Print a full block in red. U+2588 = \xe2\x96\x88 */
	snprintf(cmd, sizeof cmd,
	    "%s -o %s -w 200 40x5 bash -c '"
	    "printf \"\\033[31m\\xe2\\x96\\x88\\033[0m\"' 2>/dev/null",
	    PTYSHOT, out);
	run_cmd(cmd);

	int w, h, ch;
	unsigned char *px = stbi_load(out, &w, &h, &ch, 4);
	ASSERT_TRUE("color-png: loaded", px != NULL);

	/* Full block in cell (0,0): all pixels should be red (0xAA0000).
	 * palette256[1] = 0xAA0000 */
	int off = (3 * w + 3) * 4; /* pixel (3,3) in first cell */
	ASSERT_EQ("color-png: R=0xAA", 0xAA, px[off]);
	ASSERT_EQ("color-png: G=0x00", 0x00, px[off+1]);
	ASSERT_EQ("color-png: B=0x00", 0x00, px[off+2]);

	TEST_PASS("color correct in rendered PNG");
	stbi_image_free(px);
	unlink(out);
}

/* --- Snapshot sequence (-S) produces intermediate image --- */
static void
test_snapshot_sequence(void)
{
	const char *snap = "/tmp/ptyshot-snap-before.png";
	const char *final = "/tmp/ptyshot-snap-after.png";
	unlink(snap);
	unlink(final);

	char cmd[512];
	snprintf(cmd, sizeof cmd,
	    "%s -S %s -k enter -o %s -w 200 80x24 "
	    "bash -c 'echo BEFORE; read; echo AFTER' 2>/dev/null",
	    PTYSHOT, snap, final);
	run_cmd(cmd);

	ASSERT_TRUE("snap: before PNG exists", file_exists_nonempty(snap));
	ASSERT_TRUE("snap: after PNG exists", file_exists_nonempty(final));

	/* Both should be valid PNGs. */
	int w1, h1, ch1;
	unsigned char *px1 = stbi_load(snap, &w1, &h1, &ch1, 4);
	ASSERT_TRUE("snap: before is valid PNG", px1 != NULL);
	stbi_image_free(px1);

	int w2, h2, ch2;
	unsigned char *px2 = stbi_load(final, &w2, &h2, &ch2, 4);
	ASSERT_TRUE("snap: after is valid PNG", px2 != NULL);
	stbi_image_free(px2);

	TEST_PASS("snapshot sequence (-S) produces valid intermediate image");
	unlink(snap);
	unlink(final);
}

int
main(void)
{
	printf("=== Integration Tests ===\n");

	RUN_TEST(test_png_dimensions);
	RUN_TEST(test_png_small_terminal);
	RUN_TEST(test_text_output_content);
	RUN_TEST(test_json_output);
	RUN_TEST(test_base64_output);
	RUN_TEST(test_keystroke_delivery);
	RUN_TEST(test_record_mode);
	RUN_TEST(test_text_visible_in_png);
	RUN_TEST(test_color_in_png);
	RUN_TEST(test_snapshot_sequence);
	RUN_TEST(test_golden_hello);
	RUN_TEST(test_golden_color);
	RUN_TEST(test_golden_box);

	TEST_SUMMARY();
}
