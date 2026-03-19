/*
 * ptyshot — Standalone terminal screenshot tool with SIXEL support.
 *
 * Copyright (C) 2026 Lars-Erik Jonsson <l@jonsson.es>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Runs an application in a virtual PTY, captures the terminal output
 * (including SIXEL graphics), and renders it to a PNG file.
 *
 * Self-contained: has its own ANSI parser and SIXEL decoder.
 *
 * Usage: ptyshot [options] COLSxROWS command [args...]
 */

#ifndef VERSION
#define VERSION "0.1.0"
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"
#include "font8x16.h"

#define CELL_W		8
#define CELL_H		16
#define MAX_PARAMS	16
#define MAX_COLOURS	1024
#define MAX_IMAGES	512
#define SIXEL_MAX_W	4096
#define SIXEL_MAX_H	4096

/* Terminal cell. */
struct cell {
	uint32_t	ch;	/* Unicode codepoint */
	uint32_t	fg;
	uint32_t	bg;
	uint8_t		attr;
	uint8_t		width;	/* 1 = normal, 2 = wide (CJK), 0 = continuation */
};
#define ATTR_BRIGHT	0x01
#define ATTR_DIM	0x02
#define ATTR_REVERSE	0x04
#define ATTR_UNDERLINE	0x08

/* SIXEL image stored after decoding. */
struct sixel_img {
	uint8_t		*pixels;	/* RGBA, w*h*4 bytes */
	int		 w, h;
	int		 cell_x, cell_y; /* position in cells */
};

/* Terminal state. */
struct term {
	int		 cols, rows;
	int		 cx, cy;	/* cursor */
	struct cell	*cells;
	uint32_t	 fg, bg;
	uint8_t		 attr;

	/* Alternate screen buffer. */
	struct cell	*alt_cells;
	int		 alt_cx, alt_cy;
	int		 using_alt;	/* 1 if alt screen is active */

	/* PTY master fd for sending responses (DSR/DA). */
	int		 pty_fd;

	/* SIXEL storage. */
	struct sixel_img images[MAX_IMAGES];
	int		 nimages;

	/* True terminal simulation: per-cell SIXEL validity mask.
	 * When sixel_mask[i] == 1, the cell's SIXEL pixels are valid.
	 * Writing text to a cell clears its mask (simulating real terminal
	 * behavior where character writes erase SIXEL pixels). */
	uint8_t		*sixel_mask;
	int		 simulate_terminal;  /* 1 = true terminal mode */

	/* Synchronized output state (mode 2026). */
	int		 sync_active;  /* 1 = inside sync region */

	/* Parser state. */
	enum {
		S_GROUND,
		S_ESC,
		S_CSI,
		S_CSI_PARAM,
		S_OSC,
		S_DCS,
		S_DCS_DATA,
		S_DCS_ESC,
		S_OSC_ESC,
		S_ESC_INTER,
	} state;
	int		 params[MAX_PARAMS];
	int		 nparams;
	int		 cur_param;
	char		 esc_inter;

	/* DCS accumulation buffer. */
	char		*dcs_buf;
	size_t		 dcs_len;
	size_t		 dcs_cap;

	/* UTF-8 accumulator. */
	unsigned char	 utf8_buf[4];
	int		 utf8_len;
	int		 utf8_need;
};

/* Action types for ordered keystroke/snapshot sequence. */
enum action_type { ACT_KEY, ACT_SNAPSHOT, ACT_RECORD };
struct action {
	enum action_type type;
	char		*arg;
	int		 rec_count;    /* ACT_RECORD: number of frames */
	int		 rec_interval; /* ACT_RECORD: ms between frames */
};
#define MAX_ACTIONS	256

/* 256-color xterm palette. */
static uint32_t palette256[256];

static void
init_palette(void)
{
	static const uint32_t base16[16] = {
		0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
		0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
		0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
		0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
	};
	int i, r, g, b;

	for (i = 0; i < 16; i++)
		palette256[i] = base16[i];
	for (i = 16; i < 232; i++) {
		int idx = i - 16;
		r = idx / 36; g = (idx / 6) % 6; b = idx % 6;
		r = r ? 55 + r * 40 : 0;
		g = g ? 55 + g * 40 : 0;
		b = b ? 55 + b * 40 : 0;
		palette256[i] = (r << 16) | (g << 8) | b;
	}
	for (i = 232; i < 256; i++) {
		int v = 8 + (i - 232) * 10;
		palette256[i] = (v << 16) | (v << 8) | v;
	}
}

static struct cell *
cell_at(struct term *t, int x, int y)
{
	if (x < 0 || x >= t->cols || y < 0 || y >= t->rows)
		return (NULL);
	return (&t->cells[y * t->cols + x]);
}

static void
term_clear_sixel_cell(struct term *t, int x, int y)
{
	if (t->sixel_mask != NULL && x >= 0 && x < t->cols &&
	    y >= 0 && y < t->rows)
		t->sixel_mask[y * t->cols + x] = 0;
}

static void
term_clear_line(struct term *t, int y, int from, int to)
{
	for (int x = from; x < to && x < t->cols; x++) {
		struct cell *c = cell_at(t, x, y);
		if (c != NULL) {
			c->ch = ' ';
			c->fg = t->fg;
			c->bg = t->bg;
			c->attr = 0;
			c->width = 1;
		}
		term_clear_sixel_cell(t, x, y);
	}
}

static void
term_scroll_up(struct term *t)
{
	memmove(t->cells, t->cells + t->cols,
	    (t->rows - 1) * t->cols * sizeof(struct cell));
	term_clear_line(t, t->rows - 1, 0, t->cols);
}

static void
term_newline(struct term *t)
{
	t->cx = 0;
	t->cy++;
	if (t->cy >= t->rows) {
		term_scroll_up(t);
		t->cy = t->rows - 1;
	}
}

/*
 * Is this codepoint a wide character (takes 2 cells)?
 * Covers CJK Unified Ideographs, Hangul, fullwidth forms, and common
 * wide emoji ranges. Not exhaustive but covers practical use.
 */
static int
is_wide(uint32_t cp)
{
	if (cp >= 0x1100 && cp <= 0x115F) return 1;  /* Hangul Jamo */
	if (cp >= 0x2E80 && cp <= 0x303E) return 1;  /* CJK radicals, etc */
	if (cp >= 0x3040 && cp <= 0x33BF) return 1;  /* Hiragana..CJK compat */
	if (cp >= 0x3400 && cp <= 0x4DBF) return 1;  /* CJK Ext A */
	if (cp >= 0x4E00 && cp <= 0xA4CF) return 1;  /* CJK + Yi */
	if (cp >= 0xAC00 && cp <= 0xD7AF) return 1;  /* Hangul syllables */
	if (cp >= 0xF900 && cp <= 0xFAFF) return 1;  /* CJK compat ideographs */
	if (cp >= 0xFE30 && cp <= 0xFE6F) return 1;  /* CJK compat forms */
	if (cp >= 0xFF01 && cp <= 0xFF60) return 1;  /* Fullwidth forms */
	if (cp >= 0x20000 && cp <= 0x2FFFF) return 1; /* CJK Ext B+ */
	return 0;
}

static void
term_putch(struct term *t, uint32_t ch)
{
	int w = is_wide(ch) ? 2 : 1;

	if (t->cx + w > t->cols)
		term_newline(t);

	struct cell *c = cell_at(t, t->cx, t->cy);
	if (c != NULL) {
		c->ch = ch;
		c->fg = t->fg;
		c->bg = t->bg;
		c->attr = t->attr;
		c->width = w;
	}
	term_clear_sixel_cell(t, t->cx, t->cy);
	if (w == 2) {
		/* Mark the next cell as a continuation. */
		term_clear_sixel_cell(t, t->cx + 1, t->cy);
		struct cell *c2 = cell_at(t, t->cx + 1, t->cy);
		if (c2 != NULL) {
			c2->ch = 0;
			c2->fg = t->fg;
			c2->bg = t->bg;
			c2->attr = t->attr;
			c2->width = 0;
		}
	}
	t->cx += w;
}

/* Write a response string back to the PTY master. */
static void
term_respond(struct term *t, const char *resp)
{
	if (t->pty_fd < 0)
		return;
	size_t len = strlen(resp);
	ssize_t written = 0;
	while ((size_t)written < len) {
		ssize_t n = write(t->pty_fd, resp + written, len - written);
		if (n <= 0) break;
		written += n;
	}
}

/* Check if the screen contains a given ASCII text string. */
static int
screen_contains_text(struct term *t, const char *text)
{
	int tlen = strlen(text);
	if (tlen == 0) return 1;

	for (int y = 0; y < t->rows; y++) {
		for (int x = 0; x <= t->cols - tlen; x++) {
			int match = 1;
			for (int i = 0; i < tlen; i++) {
				struct cell *c = cell_at(t, x + i, y);
				if (c == NULL || c->ch != (uint32_t)(unsigned char)text[i]) {
					match = 0;
					break;
				}
			}
			if (match) return 1;
		}
	}
	return 0;
}

/* Parse SIXEL color definition: #Pc;Pu;Px;Py;Pz */
static void __attribute__((unused))
sixel_parse_colour(const char **cp, const char *end,
    uint32_t *colours, int *ncolours)
{
	const char *p = *cp;
	char *ep;
	int c, type, c1, c2, c3;

	c = strtol(p, &ep, 10);
	if (ep == p || c < 0 || c >= MAX_COLOURS) { *cp = end; return; }
	p = ep;
	if (c >= *ncolours) *ncolours = c + 1;

	if (p >= end || *p != ';') { *cp = p; return; }
	p++;
	type = strtol(p, &ep, 10);
	if (ep == p) { *cp = end; return; }
	p = ep;
	if (p >= end || *p != ';') { *cp = p; return; }
	p++;
	c1 = strtol(p, &ep, 10); p = ep;
	if (p >= end || *p != ';') { *cp = p; return; }
	p++;
	c2 = strtol(p, &ep, 10); p = ep;
	if (p >= end || *p != ';') { *cp = p; return; }
	p++;
	c3 = strtol(p, &ep, 10); p = ep;

	if (type == 2 && c < MAX_COLOURS) {
		/* RGB, values 0-100. */
		int r = c1 * 255 / 100;
		int g = c2 * 255 / 100;
		int b = c3 * 255 / 100;
		colours[c] = 0xFF000000 | (r << 16) | (g << 8) | b;
	}
	*cp = p;
}

/* Decode a SIXEL DCS sequence into an RGBA pixel buffer. */
static struct sixel_img
sixel_decode(const char *buf, size_t len)
{
	struct sixel_img img = {0};
	const char *cp = buf, *end = buf + len;
	uint32_t colours[MAX_COLOURS] = {0};
	int ncolours = 0;
	int dx = 0, dy = 0, max_x = 0;
	int cur_colour = 0;
	char *ep;

	/* Skip past 'q' */
	if (len < 2 || *cp != 'q') return img;
	cp++;

	/* First pass: find dimensions (scan for raster attributes). */
	int ra_w = 0, ra_h = 0;
	{
		const char *scan = cp;
		while (scan < end) {
			if (*scan == '"') {
				scan++;
				/* "Pan;Pad;Ph;Pv */
				strtol(scan, &ep, 10); scan = ep;
				if (scan < end && *scan == ';') scan++;
				strtol(scan, &ep, 10); scan = ep;
				if (scan < end && *scan == ';') scan++;
				ra_w = strtol(scan, &ep, 10); scan = ep;
				if (scan < end && *scan == ';') scan++;
				ra_h = strtol(scan, &ep, 10); scan = ep;
				break;
			}
			if (*scan >= 0x3f && *scan <= 0x7e) break; /* data */
			if (*scan == '-' || *scan == '$') break;
			scan++;
		}
	}

	/* If no raster attributes, scan manually. */
	if (ra_w <= 0 || ra_h <= 0) {
		const char *scan = cp;
		int sx = 0, sy = 0, smx = 0;
		while (scan < end) {
			char ch = *scan++;
			if (ch == '-') {
				if (sx > smx) smx = sx;
				sx = 0; sy += 6;
			} else if (ch == '$') {
				if (sx > smx) smx = sx;
				sx = 0;
			} else if (ch == '!') {
				int n = strtol(scan, &ep, 10);
				scan = ep;
				if (scan < end && *scan >= 0x3f && *scan <= 0x7e) {
					scan++;
					sx += n > 0 ? n : 1;
				}
			} else if (ch == '#') {
				while (scan < end && (*scan == ';' ||
				    (*scan >= '0' && *scan <= '9')))
					scan++;
			} else if (ch >= 0x3f && ch <= 0x7e) {
				sx++;
			}
		}
		if (sx > smx) smx = sx;
		ra_w = smx;
		ra_h = sy + 6;
	}

	if (ra_w <= 0 || ra_h <= 0 || ra_w > SIXEL_MAX_W || ra_h > SIXEL_MAX_H)
		return img;

	/* Allocate pixel buffer (RGBA). */
	img.w = ra_w;
	img.h = ra_h;
	img.pixels = calloc(ra_w * ra_h * 4, 1);
	if (img.pixels == NULL) return img;

	/* Second pass: decode pixels. */
	dx = 0; dy = 0;
	while (cp < end) {
		char ch = *cp++;
		if (ch == '#') {
			/* Colour selector or definition: #Pc or #Pc;Pu;Px;Py;Pz */
			int c = strtol(cp, &ep, 10);
			if (ep > cp && c >= 0 && c < MAX_COLOURS) {
				cur_colour = c;
				if (c >= ncolours) ncolours = c + 1;
				cp = ep;
				if (cp < end && *cp == ';') {
					/* Has definition: ;Pu;Px;Py;Pz */
					cp++;
					int type = strtol(cp, &ep, 10); cp = ep;
					if (cp < end && *cp == ';') cp++;
					int c1 = strtol(cp, &ep, 10); cp = ep;
					if (cp < end && *cp == ';') cp++;
					int c2 = strtol(cp, &ep, 10); cp = ep;
					if (cp < end && *cp == ';') cp++;
					int c3 = strtol(cp, &ep, 10); cp = ep;
					if (type == 2) {
						int r = c1 * 255 / 100;
						int g = c2 * 255 / 100;
						int b = c3 * 255 / 100;
						colours[c] = 0xFF000000 |
						    (r << 16) | (g << 8) | b;
					}
				}
			}
		} else if (ch == '!') {
			/* Repeat. */
			int n = strtol(cp, &ep, 10);
			cp = ep;
			if (cp < end) {
				char pat = *cp++ - 0x3f;
				for (int r = 0; r < n && dx < ra_w; r++, dx++) {
					for (int b = 0; b < 6; b++) {
						if (pat & (1 << b)) {
							int py = dy + b;
							if (py < ra_h && dx < ra_w) {
								int off = (py * ra_w + dx) * 4;
								uint32_t c = colours[cur_colour];
								img.pixels[off+0] = (c>>16)&0xFF;
								img.pixels[off+1] = (c>>8)&0xFF;
								img.pixels[off+2] = c&0xFF;
								img.pixels[off+3] = 0xFF;
							}
						}
					}
				}
			}
		} else if (ch == '-') {
			if (dx > max_x) max_x = dx;
			dx = 0; dy += 6;
		} else if (ch == '$') {
			if (dx > max_x) max_x = dx;
			dx = 0;
		} else if (ch == '"') {
			/* Raster attributes — skip, already parsed. */
			while (cp < end && (*cp == ';' ||
			    (*cp >= '0' && *cp <= '9')))
				cp++;
		} else if (ch >= 0x3f && ch <= 0x7e) {
			/* Sixel data character. */
			char pat = ch - 0x3f;
			for (int b = 0; b < 6; b++) {
				if (pat & (1 << b)) {
					int py = dy + b;
					if (py < ra_h && dx < ra_w) {
						int off = (py * ra_w + dx) * 4;
						uint32_t c = colours[cur_colour];
						img.pixels[off+0] = (c>>16)&0xFF;
						img.pixels[off+1] = (c>>8)&0xFF;
						img.pixels[off+2] = c&0xFF;
						img.pixels[off+3] = 0xFF;
					}
				}
			}
			dx++;
		}
	}

	return img;
}

/* Process a completed DCS sequence. */
static void
term_dcs_complete(struct term *t)
{
	if (t->dcs_len < 2 || t->dcs_buf[0] != 'q')
		goto done;

	/* SIXEL image. */
	struct sixel_img img = sixel_decode(t->dcs_buf, t->dcs_len);
	if (img.pixels != NULL && img.w > 0 && img.h > 0 &&
	    t->nimages < MAX_IMAGES) {
		img.cell_x = t->cx;
		img.cell_y = t->cy;
		t->images[t->nimages++] = img;

		/* Mark covered cells as SIXEL-valid. */
		if (t->sixel_mask != NULL) {
			int cell_w = (img.w + CELL_W - 1) / CELL_W;
			int cell_h_img = (img.h + CELL_H - 1) / CELL_H;
			for (int cy = 0; cy < cell_h_img; cy++) {
				for (int cx = 0; cx < cell_w; cx++) {
					int mx = t->cx + cx;
					int my = t->cy + cy;
					if (mx < t->cols && my < t->rows)
						t->sixel_mask[my * t->cols + mx] = 1;
				}
			}
		}

		/* Advance cursor past image. */
		int cell_h = (img.h + CELL_H - 1) / CELL_H;
		t->cy += cell_h;
		if (t->cy >= t->rows)
			t->cy = t->rows - 1;
	}

done:
	free(t->dcs_buf);
	t->dcs_buf = NULL;
	t->dcs_len = 0;
	t->dcs_cap = 0;
}

static void
dcs_append(struct term *t, char ch)
{
	if (t->dcs_len >= t->dcs_cap) {
		t->dcs_cap = t->dcs_cap ? t->dcs_cap * 2 : 4096;
		t->dcs_buf = realloc(t->dcs_buf, t->dcs_cap);
	}
	t->dcs_buf[t->dcs_len++] = ch;
}

/* Handle CSI (Control Sequence Introducer) sequences. */
static void
term_csi(struct term *t, char final)
{
	int p0 = t->nparams > 0 ? t->params[0] : 0;
	int p1 = t->nparams > 1 ? t->params[1] : 0;

	switch (final) {
	case 'A': /* CUU — cursor up */
		t->cy -= p0 > 0 ? p0 : 1;
		if (t->cy < 0) t->cy = 0;
		break;
	case 'B': /* CUD — cursor down */
		t->cy += p0 > 0 ? p0 : 1;
		if (t->cy >= t->rows) t->cy = t->rows - 1;
		break;
	case 'C': /* CUF — cursor forward */
		t->cx += p0 > 0 ? p0 : 1;
		if (t->cx >= t->cols) t->cx = t->cols - 1;
		break;
	case 'D': /* CUB — cursor back */
		t->cx -= p0 > 0 ? p0 : 1;
		if (t->cx < 0) t->cx = 0;
		break;
	case 'H': /* CUP — cursor position */
	case 'f':
		t->cy = (p0 > 0 ? p0 : 1) - 1;
		t->cx = (p1 > 0 ? p1 : 1) - 1;
		if (t->cy >= t->rows) t->cy = t->rows - 1;
		if (t->cx >= t->cols) t->cx = t->cols - 1;
		break;
	case 'J': /* ED — erase in display */
		if (p0 == 0) { /* below */
			term_clear_line(t, t->cy, t->cx, t->cols);
			for (int y = t->cy + 1; y < t->rows; y++)
				term_clear_line(t, y, 0, t->cols);
		} else if (p0 == 1) { /* above */
			for (int y = 0; y < t->cy; y++)
				term_clear_line(t, y, 0, t->cols);
			term_clear_line(t, t->cy, 0, t->cx + 1);
		} else if (p0 == 2 || p0 == 3) { /* all */
			for (int y = 0; y < t->rows; y++)
				term_clear_line(t, y, 0, t->cols);
			/* Clear all stored SIXEL images (matches real terminal
			 * behavior where CSI 2J frees the image list). */
			for (int i = 0; i < t->nimages; i++) {
				free(t->images[i].pixels);
				t->images[i].pixels = NULL;
			}
			t->nimages = 0;
		}
		break;
	case 'K': /* EL — erase in line */
		if (p0 == 0)
			term_clear_line(t, t->cy, t->cx, t->cols);
		else if (p0 == 1)
			term_clear_line(t, t->cy, 0, t->cx + 1);
		else if (p0 == 2)
			term_clear_line(t, t->cy, 0, t->cols);
		break;
	case 'c': /* DA — device attributes */
		if (t->esc_inter == '>') {
			/* DA2 (secondary device attributes). */
			term_respond(t, "\033[>41;0;0c");
		} else if (t->esc_inter == 0) {
			/* DA1 (primary device attributes).
			 * Report VT220 with SIXEL support (flag 4). */
			term_respond(t, "\033[?62;4;22c");
		}
		break;
	case 'd': /* VPA — vertical position absolute */
		t->cy = (p0 > 0 ? p0 : 1) - 1;
		if (t->cy >= t->rows) t->cy = t->rows - 1;
		break;
	case 'm': /* SGR — select graphic rendition */
		if (t->nparams == 0) {
			t->fg = 0xAAAAAA;
			t->bg = 0x0C0C16;
			t->attr = 0;
			break;
		}
		for (int i = 0; i < t->nparams; i++) {
			int p = t->params[i];
			if (p == 0) {
				t->fg = 0xAAAAAA;
				t->bg = 0x0C0C16;
				t->attr = 0;
			} else if (p == 1) {
				t->attr |= ATTR_BRIGHT;
			} else if (p == 2) {
				t->attr |= ATTR_DIM;
			} else if (p == 4) {
				t->attr |= ATTR_UNDERLINE;
			} else if (p == 7) {
				t->attr |= ATTR_REVERSE;
			} else if (p >= 30 && p <= 37) {
				t->fg = palette256[p - 30];
			} else if (p == 38 && i + 2 < t->nparams &&
			    t->params[i+1] == 5) {
				int idx = t->params[i+2];
				if (idx >= 0 && idx < 256)
					t->fg = palette256[idx];
				i += 2;
			} else if (p == 38 && i + 4 < t->nparams &&
			    t->params[i+1] == 2) {
				int r = t->params[i+2];
				int g = t->params[i+3];
				int b = t->params[i+4];
				t->fg = (r << 16) | (g << 8) | b;
				i += 4;
			} else if (p == 39) {
				t->fg = 0xAAAAAA;
			} else if (p >= 40 && p <= 47) {
				t->bg = palette256[p - 40];
			} else if (p == 48 && i + 2 < t->nparams &&
			    t->params[i+1] == 5) {
				int idx = t->params[i+2];
				if (idx >= 0 && idx < 256)
					t->bg = palette256[idx];
				i += 2;
			} else if (p == 48 && i + 4 < t->nparams &&
			    t->params[i+1] == 2) {
				int r = t->params[i+2];
				int g = t->params[i+3];
				int b = t->params[i+4];
				t->bg = (r << 16) | (g << 8) | b;
				i += 4;
			} else if (p == 49) {
				t->bg = 0x0C0C16;
			} else if (p >= 90 && p <= 97) {
				t->fg = palette256[p - 90 + 8];
			} else if (p >= 100 && p <= 107) {
				t->bg = palette256[p - 100 + 8];
			}
		}
		break;
	case 'n': /* DSR — device status report */
		if (t->esc_inter == 0 && p0 == 6) {
			/* CPR — cursor position report. */
			char resp[32];
			snprintf(resp, sizeof resp, "\033[%d;%dR",
			    t->cy + 1, t->cx + 1);
			term_respond(t, resp);
		} else if (t->esc_inter == 0 && p0 == 5) {
			/* Device status — report OK. */
			term_respond(t, "\033[0n");
		}
		break;
	case 'r': /* DECSTBM — set scrolling region (ignore for now) */
		break;
	case 'l': /* DECRST / RM */
		if (t->esc_inter == '?' && t->nparams > 0) {
			int mode = t->params[0];
			if ((mode == 1049 || mode == 1047 || mode == 47) &&
			    t->using_alt && t->alt_cells != NULL) {
				/* Leave alternate screen — swap back. */
				struct cell *tmp = t->cells;
				t->cells = t->alt_cells;
				t->alt_cells = tmp;
				if (mode == 1049) {
					/* Restore cursor. */
					t->cx = t->alt_cx;
					t->cy = t->alt_cy;
				}
				t->using_alt = 0;
			}
		}
		break;
	case 'h': /* DECSET / SM */
		if (t->esc_inter == '?' && t->nparams > 0) {
			int mode = t->params[0];
			if ((mode == 1049 || mode == 1047 || mode == 47) &&
			    !t->using_alt) {
				/* Enter alternate screen. */
				if (t->alt_cells == NULL) {
					t->alt_cells = calloc(t->cols * t->rows,
					    sizeof(struct cell));
					if (t->alt_cells == NULL) break;
				}
				if (mode == 1049) {
					/* Save cursor. */
					t->alt_cx = t->cx;
					t->alt_cy = t->cy;
				}
				/* Swap buffers. */
				struct cell *tmp = t->cells;
				t->cells = t->alt_cells;
				t->alt_cells = tmp;
				/* Clear the new (alt) screen. */
				for (int y = 0; y < t->rows; y++)
					term_clear_line(t, y, 0, t->cols);
				t->cx = 0;
				t->cy = 0;
				t->using_alt = 1;
			}
		}
		break;
	}
}

/* Decode a completed UTF-8 sequence and put the codepoint. */
static void
term_put_utf8(struct term *t)
{
	uint32_t cp = 0;
	int n = t->utf8_len;

	if (n == 2) {
		cp = ((t->utf8_buf[0] & 0x1F) << 6) |
		     (t->utf8_buf[1] & 0x3F);
	} else if (n == 3) {
		cp = ((t->utf8_buf[0] & 0x0F) << 12) |
		     ((t->utf8_buf[1] & 0x3F) << 6) |
		     (t->utf8_buf[2] & 0x3F);
	} else if (n == 4) {
		cp = ((t->utf8_buf[0] & 0x07) << 18) |
		     ((t->utf8_buf[1] & 0x3F) << 12) |
		     ((t->utf8_buf[2] & 0x3F) << 6) |
		     (t->utf8_buf[3] & 0x3F);
	}

	t->utf8_len = 0;
	t->utf8_need = 0;

	if (cp >= 0x20)
		term_putch(t, cp);
}

/* Feed one byte to the terminal state machine. */
static void
term_feed(struct term *t, unsigned char ch)
{
	/* UTF-8 continuation bytes. */
	if (t->utf8_need > 0) {
		if ((ch & 0xC0) == 0x80) {
			t->utf8_buf[t->utf8_len++] = ch;
			if (t->utf8_len == t->utf8_need) {
				term_put_utf8(t);
			}
			return;
		}
		/* Invalid continuation — reset and fall through. */
		t->utf8_len = 0;
		t->utf8_need = 0;
	}

	switch (t->state) {
	case S_GROUND:
		if (ch == 0x1B) {
			t->state = S_ESC;
		} else if (ch == '\r') {
			t->cx = 0;
		} else if (ch == '\n') {
			t->cy++;
			if (t->cy >= t->rows) {
				term_scroll_up(t);
				t->cy = t->rows - 1;
			}
		} else if (ch == '\t') {
			t->cx = (t->cx + 8) & ~7;
			if (t->cx >= t->cols) t->cx = t->cols - 1;
		} else if (ch == '\b') {
			if (t->cx > 0) t->cx--;
		} else if (ch == 0x07 || ch < 0x20) {
			/* Bell and other C0 controls: ignore. */
		} else if (ch >= 0xC0 && ch <= 0xF7) {
			/* UTF-8 lead byte. */
			t->utf8_buf[0] = ch;
			t->utf8_len = 1;
			if (ch < 0xE0)
				t->utf8_need = 2;
			else if (ch < 0xF0)
				t->utf8_need = 3;
			else
				t->utf8_need = 4;
		} else if (ch >= 0x20 && ch < 0x80) {
			term_putch(t, ch);
		}
		break;

	case S_ESC:
		if (ch == '[') {
			t->state = S_CSI;
			t->nparams = 0;
			t->cur_param = -1;
			t->esc_inter = 0;
		} else if (ch == ']') {
			t->state = S_OSC;
		} else if (ch == 'P') {
			/* DCS — Device Control String. */
			t->state = S_DCS;
			free(t->dcs_buf);
			t->dcs_buf = NULL;
			t->dcs_len = 0;
			t->dcs_cap = 0;
		} else if (ch == '(' || ch == ')') {
			t->state = S_ESC_INTER;
		} else if (ch == '=') {
			t->state = S_GROUND; /* DECKPAM */
		} else if (ch == '>') {
			t->state = S_GROUND; /* DECKPNM */
		} else if (ch == '\\') {
			t->state = S_GROUND; /* ST */
		} else {
			t->state = S_GROUND;
		}
		break;

	case S_ESC_INTER:
		/* Consume one byte after ESC ( or ESC ) */
		t->state = S_GROUND;
		break;

	case S_CSI:
		if (ch == '?') {
			t->esc_inter = '?';
			t->state = S_CSI_PARAM;
		} else if (ch == '>') {
			t->esc_inter = '>';
			t->state = S_CSI_PARAM;
		} else if (ch >= '0' && ch <= '9') {
			t->cur_param = ch - '0';
			t->state = S_CSI_PARAM;
		} else if (ch == ';') {
			if (t->nparams < MAX_PARAMS)
				t->params[t->nparams++] = 0;
			t->state = S_CSI_PARAM;
		} else if (ch >= 0x40 && ch <= 0x7E) {
			/* Final byte with no params. */
			term_csi(t, ch);
			t->state = S_GROUND;
		} else {
			t->state = S_GROUND;
		}
		break;

	case S_CSI_PARAM:
		if (ch >= '0' && ch <= '9') {
			if (t->cur_param < 0) t->cur_param = 0;
			t->cur_param = t->cur_param * 10 + (ch - '0');
		} else if (ch == ';') {
			if (t->nparams < MAX_PARAMS)
				t->params[t->nparams++] = t->cur_param > 0 ? t->cur_param : 0;
			t->cur_param = -1;
		} else if (ch >= 0x40 && ch <= 0x7E) {
			/* Final byte. */
			if (t->nparams < MAX_PARAMS)
				t->params[t->nparams++] = t->cur_param > 0 ? t->cur_param : 0;
			term_csi(t, ch);
			t->state = S_GROUND;
		} else {
			/* Intermediate or unknown. */
		}
		break;

	case S_OSC:
		if (ch == 0x1B)
			t->state = S_OSC_ESC;
		else if (ch == 0x07) /* BEL terminates OSC */
			t->state = S_GROUND;
		/* else: accumulate, but we ignore OSC content. */
		break;

	case S_OSC_ESC:
		if (ch == '\\')
			t->state = S_GROUND;
		else
			t->state = S_OSC; /* wasn't ST, back to OSC */
		break;

	case S_DCS:
		/* Skip DCS params until 'q' or data starts. */
		if (ch == 'q' || (ch >= 0x40 && ch <= 0x7E)) {
			dcs_append(t, ch);
			t->state = S_DCS_DATA;
		} else if (ch >= 0x30 && ch <= 0x3F) {
			/* DCS parameter byte, skip. */
		} else if (ch == 0x1B) {
			t->state = S_DCS_ESC;
		} else {
			/* Unknown, abort DCS. */
			free(t->dcs_buf);
			t->dcs_buf = NULL;
			t->dcs_len = 0;
			t->dcs_cap = 0;
			t->state = S_GROUND;
		}
		break;

	case S_DCS_DATA:
		if (ch == 0x1B) {
			t->state = S_DCS_ESC;
		} else if (ch == 0x07) {
			/* BEL can terminate DCS in some terminals. */
			term_dcs_complete(t);
			t->state = S_GROUND;
		} else {
			dcs_append(t, ch);
		}
		break;

	case S_DCS_ESC:
		if (ch == '\\') {
			/* ST — DCS complete. */
			term_dcs_complete(t);
			t->state = S_GROUND;
		} else {
			/* Not ST, put ESC back. */
			dcs_append(t, 0x1B);
			dcs_append(t, ch);
			t->state = S_DCS_DATA;
		}
		break;
	}
}

/* Render terminal to RGBA pixel buffer. */
static unsigned char *
render(struct term *t, int *out_w, int *out_h)
{
	int pw = t->cols * CELL_W;
	int ph = t->rows * CELL_H;
	unsigned char *px = calloc(pw * ph * 4, 1);
	if (px == NULL) return NULL;

	/* Fill background. */
	for (int i = 0; i < pw * ph; i++) {
		px[i*4+0] = 0x0C;
		px[i*4+1] = 0x0C;
		px[i*4+2] = 0x16;
		px[i*4+3] = 0xFF;
	}

	/* Render text. */
	for (int y = 0; y < t->rows; y++) {
		for (int x = 0; x < t->cols; x++) {
			struct cell *c = cell_at(t, x, y);

			/* Skip continuation cells (second half of wide char). */
			if (c->width == 0)
				continue;

			uint32_t fg = c->fg, bg = c->bg;

			if (c->attr & ATTR_REVERSE) {
				uint32_t tmp = fg; fg = bg; bg = tmp;
			}

			const unsigned char *glyph = font8x16_glyph(c->ch);

			for (int gy = 0; gy < CELL_H; gy++) {
				unsigned char row = glyph[gy];
				for (int gx = 0; gx < CELL_W; gx++) {
					int px_x = x * CELL_W + gx;
					int px_y = y * CELL_H + gy;
					int off = (px_y * pw + px_x) * 4;
					uint32_t color;

					if (row & (0x80 >> gx)) {
						color = fg;
						if (c->attr & ATTR_BRIGHT) {
							int r = (color>>16)&0xFF;
							int g = (color>>8)&0xFF;
							int b = color&0xFF;
							r += (255-r)/3;
							g += (255-g)/3;
							b += (255-b)/3;
							color = (r<<16)|(g<<8)|b;
						}
					} else {
						color = bg;
					}

					px[off+0] = (color>>16) & 0xFF;
					px[off+1] = (color>>8) & 0xFF;
					px[off+2] = color & 0xFF;
					px[off+3] = 0xFF;
				}
			}

			/* Underline. */
			if (c->attr & ATTR_UNDERLINE) {
				int ul_y = y * CELL_H + 14;
				for (int gx = 0; gx < CELL_W; gx++) {
					int off = (ul_y * pw + x*CELL_W + gx) * 4;
					px[off+0] = (fg>>16) & 0xFF;
					px[off+1] = (fg>>8) & 0xFF;
					px[off+2] = fg & 0xFF;
					px[off+3] = 0xFF;
				}
			}
		}
	}

	/* Composite SIXEL images on top.
	 * In simulation mode (-T), only show SIXEL pixels for cells where
	 * the SIXEL mask is still valid (no text written since SIXEL placed).
	 * This matches real terminal behavior where character writes erase
	 * the underlying SIXEL pixel data. */
	for (int i = 0; i < t->nimages; i++) {
		struct sixel_img *img = &t->images[i];
		if (img->pixels == NULL) continue;

		int base_x = img->cell_x * CELL_W;
		int base_y = img->cell_y * CELL_H;

		for (int iy = 0; iy < img->h; iy++) {
			for (int ix = 0; ix < img->w; ix++) {
				int src = (iy * img->w + ix) * 4;
				if (img->pixels[src+3] == 0) continue;

				int dx = base_x + ix;
				int dy = base_y + iy;
				if (dx >= pw || dy >= ph) continue;

				/* In simulation mode, check if this cell's
				 * SIXEL data has been overwritten by text. */
				if (t->simulate_terminal && t->sixel_mask) {
					int cell_x = dx / CELL_W;
					int cell_y = dy / CELL_H;
					if (cell_x < t->cols && cell_y < t->rows &&
					    !t->sixel_mask[cell_y * t->cols + cell_x])
						continue; /* text overwrote SIXEL */
				}

				int dst = (dy * pw + dx) * 4;
				px[dst+0] = img->pixels[src+0];
				px[dst+1] = img->pixels[src+1];
				px[dst+2] = img->pixels[src+2];
				px[dst+3] = 0xFF;
			}
		}
	}

	*out_w = pw;
	*out_h = ph;
	return px;
}

static int take_snapshot(struct term *, const char *);

/*
 * Record mode: capture frames from PTY output.
 *
 * When simulate_terminal is off: capture count frames at interval_ms apart.
 * When simulate_terminal is on: capture after EVERY read() from the PTY,
 * showing the exact intermediate states a real terminal would display.
 * This reveals SIXEL flicker: frames where ratatui's character write has
 * erased SIXEL pixels but the SIXEL re-emission hasn't arrived yet.
 *
 * Returns -1 on EOF, 0 on success.
 */
static int
record_frames(struct term *t, int fd, int count, int interval_ms,
    const char *prefix)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	unsigned char buf[65536];
	ssize_t n;
	char filename[512];

	if (t->simulate_terminal) {
		/* Per-read recording: snapshot after every read() to
		 * capture intermediate terminal states. */
		int frame = 0;
		int total_ms = count * interval_ms;
		struct timespec start;
		clock_gettime(CLOCK_MONOTONIC, &start);

		for (;;) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			long elapsed =
			    (now.tv_sec - start.tv_sec) * 1000 +
			    (now.tv_nsec - start.tv_nsec) / 1000000;
			if (elapsed >= total_ms)
				break;

			int ret = poll(&pfd, 1, 5);
			if (ret > 0 && (pfd.revents & POLLIN)) {
				n = read(fd, buf, sizeof buf);
				if (n <= 0)
					return (-1);
				for (ssize_t i = 0; i < n; i++)
					term_feed(t, buf[i]);

				/* Snapshot after every read. */
				snprintf(filename, sizeof filename,
				    "%s_%03d.png", prefix, frame);
				take_snapshot(t, filename);
				frame++;
			} else if (ret > 0 &&
			    (pfd.revents & (POLLHUP | POLLERR))) {
				return (-1);
			}
		}
		fprintf(stderr, "ptyshot: recorded %d frames\n", frame);
	} else {
		/* Interval recording: snapshot at fixed intervals. */
		struct timespec frame_start;

		for (int frame = 0; frame < count; frame++) {
			clock_gettime(CLOCK_MONOTONIC, &frame_start);

			/* Drain all available PTY data (non-blocking). */
			for (;;) {
				int ret = poll(&pfd, 1, 0);
				if (ret <= 0)
					break;
				if (pfd.revents & POLLIN) {
					n = read(fd, buf, sizeof buf);
					if (n <= 0)
						return (-1);
					for (ssize_t i = 0; i < n; i++)
						term_feed(t, buf[i]);
				} else if (pfd.revents &
				    (POLLHUP | POLLERR)) {
					return (-1);
				}
			}

			/* Take snapshot. */
			snprintf(filename, sizeof filename,
			    "%s_%03d.png", prefix, frame);
			take_snapshot(t, filename);

			/* Wait for next frame interval. */
			for (;;) {
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				long elapsed =
				    (now.tv_sec - frame_start.tv_sec) * 1000 +
				    (now.tv_nsec - frame_start.tv_nsec) /
				    1000000;
				if (elapsed >= interval_ms)
					break;
				int remaining =
				    interval_ms - (int)elapsed;
				int ret = poll(&pfd, 1, remaining);
				if (ret > 0 && (pfd.revents & POLLIN)) {
					n = read(fd, buf, sizeof buf);
					if (n <= 0)
						return (-1);
					for (ssize_t i = 0; i < n; i++)
						term_feed(t, buf[i]);
				} else if (ret > 0 &&
				    (pfd.revents & (POLLHUP | POLLERR))) {
					return (-1);
				}
			}
		}
	}
	return (0);
}

/* Take a snapshot: render current screen and write PNG. */
static int
take_snapshot(struct term *t, const char *filename)
{
	int pw, ph;
	unsigned char *pixels = render(t, &pw, &ph);
	if (pixels == NULL) {
		fprintf(stderr, "ptyshot: render failed for %s\n", filename);
		return -1;
	}

	if (!stbi_write_png(filename, pw, ph, 4, pixels, pw * 4)) {
		fprintf(stderr, "ptyshot: failed to write %s\n", filename);
		free(pixels);
		return -1;
	}

	fprintf(stderr, "ptyshot: %s (%dx%d px)\n", filename, pw, ph);
	free(pixels);
	return 0;
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: ptyshot [-V] [-o output.png] [-d delay_ms] [-k keystroke]\n"
	    "       [-w settle_ms] [-m min_ms] [-W wait_text] [-S snap.png]\n"
	    "       [-R count:interval_ms:prefix] [-T] COLSxROWS command [args...]\n"
	    "\n"
	    "  -R count:interval_ms:prefix\n"
	    "       Record mode: capture count frames at interval_ms apart.\n"
	    "       Saves as prefix_000.png, prefix_001.png, etc.\n"
	    "       Reads PTY data between frames without waiting for settle.\n"
	    "  -T   True terminal simulation: text writes erase SIXEL pixels.\n"
	    "       Use with -R to capture SIXEL flicker bugs.\n");
	exit(1);
}

static const char *
key_to_seq(const char *key)
{
	if (strcasecmp(key, "enter") == 0) return "\r";
	if (strcasecmp(key, "tab") == 0) return "\t";
	if (strcasecmp(key, "escape") == 0 || strcasecmp(key, "esc") == 0) return "\033";
	if (strcasecmp(key, "space") == 0) return " ";
	if (strcasecmp(key, "backspace") == 0) return "\177";
	if (strcasecmp(key, "up") == 0) return "\033[A";
	if (strcasecmp(key, "down") == 0) return "\033[B";
	if (strcasecmp(key, "right") == 0) return "\033[C";
	if (strcasecmp(key, "left") == 0) return "\033[D";
	if (strlen(key) == 6 && strncmp(key, "ctrl-", 5) == 0) {
		static char ctrl[2];
		ctrl[0] = key[5] - 'a' + 1;
		ctrl[1] = '\0';
		return ctrl;
	}
	return key;
}

/*
 * Hash the visible screen state (cells + SIXEL image count).
 * Used to detect when the screen has stopped changing.
 */
static uint64_t
screen_hash(struct term *t)
{
	uint64_t h = 0x517cc1b727220a95ULL; /* FNV offset basis */
	int ncells = t->cols * t->rows;

	for (int i = 0; i < ncells; i++) {
		uint64_t v = t->cells[i].ch;
		v = (v << 24) | (t->cells[i].fg & 0xFFFFFF);
		h ^= v;
		h *= 0x100000001b3ULL; /* FNV prime */
		h ^= t->cells[i].bg;
		h *= 0x100000001b3ULL;
	}
	/* Note: SIXEL image count is NOT included in the hash.
	 * Apps that re-emit SIXEL every frame (e.g. for overlay compositing)
	 * would never settle if we counted images. The cell content alone
	 * determines whether the visible text layer has stabilized. */
	return h;
}

/*
 * Read PTY output and feed to terminal until the screen settles.
 *
 * Settle means: no new output for settle_ms AND the parser is in
 * ground state (not mid-DCS/SIXEL). For continuous-animation apps,
 * settle also triggers when data is still arriving but the visible
 * screen hasn't changed for two consecutive reads — meaning the app
 * is redrawing the same frame.
 *
 * If wait_first is set, wait up to 10s for the first byte of output.
 * If min_ms > 0, keep reading for at least that many ms.
 * If wait_text is non-NULL, wait until that text appears (30s cap).
 */
static int
drain_pty(struct term *t, int fd, int settle_ms, int wait_first, int min_ms,
    const char *wait_text)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	unsigned char buf[65536];
	ssize_t n;
	int got_data = 0;
	int content_found = (wait_text == NULL);
	uint64_t prev_hash = screen_hash(t);
	int stable_count = 0;
	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);

	for (;;) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		long elapsed = (now.tv_sec - start.tv_sec) * 1000 +
		    (now.tv_nsec - start.tv_nsec) / 1000000;

		/* Check if we've passed the minimum read time. */
		int past_min = (min_ms <= 0) || (elapsed >= min_ms);

		/* Hard timeout: 30s for content wait, prevents infinite hang. */
		if (!content_found && elapsed >= 30000) {
			fprintf(stderr,
			    "ptyshot: warning: timed out waiting for \"%s\"\n",
			    wait_text);
			content_found = 1;
		}

		int timeout;
		if (!got_data && wait_first)
			timeout = 10000;
		else if (!content_found)
			timeout = 1000;
		else if (!past_min)
			timeout = 1000;
		else
			timeout = settle_ms;

		int ret = poll(&pfd, 1, timeout);
		if (ret <= 0) {
			if (!content_found) continue;
			if (!past_min) continue;
			/* Don't settle mid-DCS (e.g. partial SIXEL data). */
			if (t->state != S_GROUND && elapsed < 10000)
				continue;
			break; /* timeout — settled */
		}
		if (pfd.revents & POLLIN) {
			n = read(fd, buf, sizeof buf);
			if (n <= 0)
				return (-1); /* EOF or error */
			for (ssize_t i = 0; i < n; i++)
				term_feed(t, buf[i]);
			got_data = 1;

			/* Check for wait text after processing new data. */
			if (!content_found &&
			    screen_contains_text(t, wait_text))
				content_found = 1;

			/* Screen-stability detection: if the app is still
			 * sending data but the visible screen hasn't changed,
			 * the frame is complete. This handles continuous-
			 * animation apps that redraw the same content. */
			if (content_found && past_min &&
			    t->state == S_GROUND) {
				uint64_t h = screen_hash(t);
				if (h == prev_hash) {
					stable_count++;
					if (stable_count >= 2)
						break; /* screen stable */
				} else {
					stable_count = 0;
					prev_hash = h;
				}
			}
		} else if (pfd.revents & (POLLHUP | POLLERR)) {
			/* Child exited, drain anything left. */
			while ((n = read(fd, buf, sizeof buf)) > 0) {
				for (ssize_t i = 0; i < n; i++)
					term_feed(t, buf[i]);
			}
			if (!content_found)
				fprintf(stderr,
				    "ptyshot: warning: child exited before "
				    "\"%s\" appeared\n", wait_text);
			return (-1); /* EOF */
		}
	}
	return (0); /* timeout — settled */
}

int
main(int argc, char **argv)
{
	int		 opt, master_fd, status;
	pid_t		 child;
	unsigned int	 cols = 80, rows = 24;
	char		*output = "screenshot.png";
	int		 key_delay = 100;
	int		 settle_wait = 500;
	int		 min_read = 0;
	char		*wait_text = NULL;
	int		 simulate = 0;
	struct action	 actions[MAX_ACTIONS];
	int		 nactions = 0;
	struct winsize	 ws;

	init_palette();

	while ((opt = getopt(argc, argv, "+o:d:k:w:m:W:S:R:ThV")) != -1) {
		switch (opt) {
		case 'V':
			printf("ptyshot %s\n", VERSION);
			exit(0);
		case 'o':
			output = optarg;
			break;
		case 'd':
			key_delay = atoi(optarg);
			break;
		case 'k':
			if (nactions < MAX_ACTIONS) {
				actions[nactions].type = ACT_KEY;
				actions[nactions].arg = optarg;
				nactions++;
			}
			break;
		case 'w':
			settle_wait = atoi(optarg);
			break;
		case 'm':
			min_read = atoi(optarg);
			break;
		case 'W':
			wait_text = optarg;
			break;
		case 'S':
			if (nactions < MAX_ACTIONS) {
				actions[nactions].type = ACT_SNAPSHOT;
				actions[nactions].arg = optarg;
				nactions++;
			}
			break;
		case 'T':
			simulate = 1;
			break;
		case 'R':
			if (nactions < MAX_ACTIONS) {
				/* Format: count:interval_ms:prefix */
				actions[nactions].type = ACT_RECORD;
				actions[nactions].rec_count = 10;
				actions[nactions].rec_interval = 50;
				actions[nactions].arg = NULL;
				char *r_arg = strdup(optarg);
				char *colon1 = strchr(r_arg, ':');
				if (colon1) {
					*colon1 = '\0';
					actions[nactions].rec_count =
					    atoi(r_arg);
					char *rest = colon1 + 1;
					char *colon2 = strchr(rest, ':');
					if (colon2) {
						*colon2 = '\0';
						actions[nactions].rec_interval =
						    atoi(rest);
						actions[nactions].arg =
						    colon2 + 1;
					} else {
						actions[nactions].rec_interval =
						    atoi(rest);
					}
				} else {
					actions[nactions].rec_count =
					    atoi(r_arg);
				}
				if (actions[nactions].arg == NULL)
					actions[nactions].arg = "record";
				nactions++;
			}
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();

	if (sscanf(argv[0], "%ux%u", &cols, &rows) != 2) {
		fprintf(stderr, "ptyshot: invalid dimensions: %s\n", argv[0]);
		exit(1);
	}
	argv++;
	argc--;

	/* Allocate terminal state. */
	struct term t = {0};
	t.cols = cols;
	t.rows = rows;
	t.fg = 0xAAAAAA;
	t.bg = 0x0C0C16;
	t.pty_fd = -1;
	t.simulate_terminal = simulate;
	t.cells = calloc(cols * rows, sizeof(struct cell));
	if (t.cells == NULL) { perror("calloc"); exit(1); }
	if (simulate) {
		t.sixel_mask = calloc(cols * rows, 1);
		if (t.sixel_mask == NULL) { perror("calloc"); exit(1); }
	}

	/* Initialize cells. */
	for (unsigned int i = 0; i < cols * rows; i++) {
		t.cells[i].ch = ' ';
		t.cells[i].fg = t.fg;
		t.cells[i].bg = t.bg;
		t.cells[i].width = 1;
	}

	/* Create PTY. */
	memset(&ws, 0, sizeof ws);
	ws.ws_col = cols;
	ws.ws_row = rows;
	ws.ws_xpixel = cols * CELL_W;
	ws.ws_ypixel = rows * CELL_H;

	child = forkpty(&master_fd, NULL, NULL, &ws);
	if (child == -1) { perror("forkpty"); exit(1); }

	if (child == 0) {
		setenv("TERM", "xterm-256color", 1);
		execvp(argv[0], argv);
		perror(argv[0]);
		_exit(127);
	}

	fcntl(master_fd, F_SETFL, O_NONBLOCK);
	t.pty_fd = master_fd;

	/* Read initial output (wait for app to start). */
	int eof = drain_pty(&t, master_fd, settle_wait, 1, min_read,
	    wait_text);

	/* Execute actions: keystrokes and snapshots. */
	for (int i = 0; i < nactions && eof == 0; i++) {
		if (actions[i].type == ACT_KEY) {
			const char *seq = key_to_seq(actions[i].arg);
			if (write(master_fd, seq, strlen(seq)) == -1)
				break;
			/* Sleep for the requested delay, draining PTY output
			 * in the background so the pipe buffer doesn't fill
			 * (which would block the child).  We use a tight
			 * poll+read loop during the delay instead of plain
			 * usleep, so large outputs (SIXEL) are consumed. */
			{
				int delay = key_delay > 0 ? key_delay : 100;
				struct timespec ts_start;
				clock_gettime(CLOCK_MONOTONIC, &ts_start);
				struct pollfd pfd = { .fd = master_fd,
				    .events = POLLIN };
				unsigned char buf[65536];
				for (;;) {
					struct timespec ts_now;
					clock_gettime(CLOCK_MONOTONIC, &ts_now);
					long elapsed =
					    (ts_now.tv_sec - ts_start.tv_sec)
					    * 1000 +
					    (ts_now.tv_nsec - ts_start.tv_nsec)
					    / 1000000;
					if (elapsed >= delay)
						break;
					long remain = delay - elapsed;
					int ret = poll(&pfd, 1,
					    remain < 10 ? (int)remain : 10);
					if (ret > 0 &&
					    (pfd.revents & POLLIN)) {
						ssize_t n = read(master_fd,
						    buf, sizeof buf);
						if (n <= 0) {
							eof = -1;
							break;
						}
						for (ssize_t j = 0; j < n; j++)
							term_feed(&t, buf[j]);
					}
				}
			}
		} else if (actions[i].type == ACT_SNAPSHOT) {
			take_snapshot(&t, actions[i].arg);
		} else if (actions[i].type == ACT_RECORD) {
			eof = record_frames(&t, master_fd,
			    actions[i].rec_count,
			    actions[i].rec_interval,
			    actions[i].arg);
		}
	}

	/* Clean up child. */
	kill(child, SIGTERM);
	waitpid(child, &status, 0);
	close(master_fd);

	/* Render final output. */
	take_snapshot(&t, output);

	free(t.cells);
	free(t.alt_cells);
	free(t.sixel_mask);
	for (int i = 0; i < t.nimages; i++)
		free(t.images[i].pixels);
	free(t.dcs_buf);

	return 0;
}
