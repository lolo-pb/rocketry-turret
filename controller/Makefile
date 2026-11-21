CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
sources = main.c parser.c receivers.c state.c dashboard_ipc.c automatic.c manual.c tracker.c servos.c telemega.c logger.c

.PHONY: controller clean test
controller: build/controller

build/controller: $(sources) controller.h servos.h telemega.h logger.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 $(sources) $(LDFLAGS) -pthread -lm -o $@

build:
	mkdir -p $@

test: controller
	python3 ../tests/test_controller.py

clean:
	$(RM) -r build
