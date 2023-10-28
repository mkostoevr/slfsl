.PHONY: all run

all: test

test: test/list.exe
	test/list.exe

debug: test/list.exe
	gdb --args $<

test/list.exe: test/list.c list.h
	clang $< -O3 -o $@ -lpthread -mcx16
