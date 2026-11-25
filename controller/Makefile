CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -I. -Ireceivers -Iservos -Ilogging
sources = main.c inputs.c state.c receivers/receivers.c receivers/featherweight.c receivers/telemega.c dashboard/dashboard.c modes/automatic.c modes/manual.c modes/tracker.c modes/modes.c servos/servos.c logging/logger.c

.PHONY: controller clean test
controller: build/controller

build/controller: $(sources) main.h inputs.h state.h receivers/receivers.h receivers/parsers.h dashboard/dashboard.h modes/modes.h servos/servos.h logging/logger.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 $(sources) $(LDFLAGS) -pthread -lm -o $@

build:
	mkdir -p $@

test: controller
	python3 ../tests/test_controller.py

clean:
	$(RM) -r build
