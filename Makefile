# ptyshot — Standalone terminal screenshot tool with SIXEL support
#
# Copyright (C) 2026 Lars-Erik Jonsson <l@jonsson.es>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

VERSION = 0.2.0

CC ?= gcc
CFLAGS = -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -DVERSION=\"$(VERSION)\"
LDFLAGS =
ifeq ($(shell uname -s),Darwin)
LIBS = -lm
else
LIBS = -lutil -lm
endif

SRCS = ptyshot.c
OBJS = $(SRCS:.c=.o)
TARGET = ptyshot

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	@echo "=== Test: basic text rendering ==="
	./$(TARGET) -o /tmp/ptyshot-test.png -w 200 80x24 bash -c 'echo "Hello from ptyshot!"; echo "SIXEL support built-in."'
	@echo "Output: /tmp/ptyshot-test.png"
	@ls -la /tmp/ptyshot-test.png
	@echo ""
	@echo "=== Test: minimum read time (-m) ==="
	./$(TARGET) -o /tmp/ptyshot-minread.png -m 500 -w 200 80x24 bash -c 'echo "frame1"; sleep 0.2; echo "frame2"'
	@test -s /tmp/ptyshot-minread.png && echo "PASS: -m produced non-empty PNG" || (echo "FAIL: -m produced empty/missing PNG"; exit 1)
	@echo ""
	@echo "=== Test: wait for text (-W) ==="
	./$(TARGET) -o /tmp/ptyshot-waittext.png -W "READY" -w 100 80x24 bash -c 'echo "loading..."; sleep 0.3; echo "READY"'
	@test -s /tmp/ptyshot-waittext.png && echo "PASS: -W produced non-empty PNG" || (echo "FAIL: -W produced empty/missing PNG"; exit 1)
	@echo ""
	@echo "=== Test: record mode (-R) ==="
	@rm -f /tmp/ptyshot-rec_000.png /tmp/ptyshot-rec_001.png /tmp/ptyshot-rec_002.png
	./$(TARGET) -o /tmp/ptyshot-rec-final.png -w 200 -k 'enter' -R 3:100:/tmp/ptyshot-rec 80x24 bash -c 'read -p "go: "; echo "line1"; sleep 0.2; echo "line2"; sleep 0.2; echo "line3"; sleep 1'
	@test -s /tmp/ptyshot-rec_000.png && echo "PASS: -R frame 0 exists" || (echo "FAIL: -R frame 0 missing"; exit 1)
	@test -s /tmp/ptyshot-rec_001.png && echo "PASS: -R frame 1 exists" || (echo "FAIL: -R frame 1 missing"; exit 1)
	@test -s /tmp/ptyshot-rec_002.png && echo "PASS: -R frame 2 exists" || (echo "FAIL: -R frame 2 missing"; exit 1)
	@echo ""
	@echo "=== Test: combined -W and -m ==="
	./$(TARGET) -o /tmp/ptyshot-combined.png -W "DONE" -m 300 -w 100 80x24 bash -c 'echo "step1"; sleep 0.1; echo "step2"; sleep 0.1; echo "DONE"'
	@test -s /tmp/ptyshot-combined.png && echo "PASS: -W + -m produced non-empty PNG" || (echo "FAIL: -W + -m produced empty/missing PNG"; exit 1)
	@echo ""
	@echo "=== Test: ctrl-key case insensitive (Ctrl-C, CTRL-C, ctrl-c) ==="
	./$(TARGET) -o /tmp/ptyshot-ctrlc1.png -k 'ctrl-c' -w 200 80x24 bash -c 'cat; echo unreachable'
	@test -s /tmp/ptyshot-ctrlc1.png && echo "PASS: ctrl-c works" || (echo "FAIL: ctrl-c"; exit 1)
	./$(TARGET) -o /tmp/ptyshot-ctrlc2.png -k 'Ctrl-C' -w 200 80x24 bash -c 'cat; echo unreachable'
	@test -s /tmp/ptyshot-ctrlc2.png && echo "PASS: Ctrl-C works" || (echo "FAIL: Ctrl-C"; exit 1)
	./$(TARGET) -o /tmp/ptyshot-ctrlc3.png -k 'CTRL-C' -w 200 80x24 bash -c 'cat; echo unreachable'
	@test -s /tmp/ptyshot-ctrlc3.png && echo "PASS: CTRL-C works" || (echo "FAIL: CTRL-C"; exit 1)
	@echo ""
	@echo "=== Test: dim text rendering (SGR 2) ==="
	./$(TARGET) -o /tmp/ptyshot-dim.png -w 200 80x24 bash -c 'printf "\033[2mDIM TEXT\033[0m NORMAL"'
	@test -s /tmp/ptyshot-dim.png && echo "PASS: dim text rendered" || (echo "FAIL: dim text"; exit 1)

.PHONY: all clean test
