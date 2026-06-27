CFLAGS := $(shell cat compile_flags.txt)

picknrun: picknrun.c
	gcc $(CFLAGS) picknrun.c -o picknrun -lncurses

.PHONY: test
test: picknrun
	gcc $(CFLAGS) -g test.c -o test
	./test
