##
# Graphing Monitor
#
# @file
# @version 0.1

all: bin/graphing_monitor

bin/graphing_monitor: src/main.c src/timespec.c src/terminal_dots.c
	$(CC) $(CFLAGS) $^ -o $@ -lm -lpthread -Iinclude

clean:
	rm -f bin/*
# end
