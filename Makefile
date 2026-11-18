CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2

.PHONY: all clean test
all: mshell

mshell: minish.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f mshell

test: mshell
	sh tests/mshell-smoke.sh
	sh tests/compat.sh
	python3 tests/job-control.py
