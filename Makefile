.PHONY: all run

all: run

run: listdb.exe
	./listdb.exe

listdb.exe: main.c
	clang main.c -o listdb.exe -lpthread
