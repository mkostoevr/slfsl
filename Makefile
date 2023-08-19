.PHONY: all run

all: run

run: test.exe
	./test.exe

debug: test.exe
	gdb --args ./test.exe

test.exe: main.c
	clang main.c -O3 -o test.exe -lpthread
