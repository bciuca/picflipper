# SPDX-License-Identifier: MIT
# Host-side unit tests. Compiled and run on the dev machine with plain cc/clang
# (NOT ufbt, NOT the Flipper). Each test #includes one product .c from ../src
# and links against the minimal SDK stubs in ./stubs.
#
#   make test    build and run every test (non-zero exit on any failure)
#   make clean   remove build artifacts
#
# Include search: -I../src resolves the `#include "<unit>.c"` in each test and
# the product headers; -Istubs resolves the SDK headers (<furi.h>, <gui/*>,
# <storage/*>, <furi_hal_*>).

CC     ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -I../src -Istubs
BINDIR := build

TESTS := test_sha256 test_dump_writer test_pic_icsp test_view_about \
         test_prog_progress test_ui_wrap
BINS  := $(addprefix $(BINDIR)/,$(TESTS))

.PHONY: test clean
test: $(BINS)
	@fail=0; \
	for t in $(TESTS); do \
	  echo "==> $$t"; \
	  ./$(BINDIR)/$$t || fail=1; \
	  echo; \
	done; \
	if [ $$fail -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi; \
	echo "ALL TESTS PASSED"

$(BINDIR)/%: %.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR)
