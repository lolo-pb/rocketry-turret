CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -I. -Ireceivers -Iservos -Ilogging
sources = main.c state.c receivers/receivers.c receivers/featherweight.c receivers/telemega.c dashboard/dashboard_ipc.c modes/automatic.c modes/manual.c modes/tracker.c servos/servos.c logging/logger.c

.PHONY: controller clean test
controller: build/controller

build/controller: $(sources) controller.h servos/servos.h receivers/parsers.h logging/logger.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 $(sources) $(LDFLAGS) -pthread -lm -o $@

build:
	mkdir -p $@

test: controller
	python3 ../tests/test_controller.py

clean:
	$(RM) -r build
