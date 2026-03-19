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

VERSION = 0.1.0

CC ?= gcc
CFLAGS = -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter -DVERSION=\"$(VERSION)\"
LDFLAGS =
LIBS = -lutil -lm

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

.PHONY: all clean test
