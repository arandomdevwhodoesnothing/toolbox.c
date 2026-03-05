CC     ?= gcc
CFLAGS := -std=c11 -ffreestanding -nostdlib -nostdinc \
	-Iinclude -Wall -Wextra -O2 \
	-fno-builtin -fno-stack-protector

LINKFLAGS := -fPIE -pie

CSRC   := src/start.c   \
	src/malloc.c  \
	src/stdio.c   \
	src/printf.c  \
	src/string.c

SSRC   := src/arch/aarch64/syscall.s

COBJ   := $(CSRC:.c=.o)
SOBJ   := $(SSRC:.s=.o)
OBJ    := $(COBJ) $(SOBJ)

LIB    := libtool.a
CRT    := crt0.o

.PHONY: all clean test demo build

all: $(LIB) $(CRT)

$(COBJ): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SOBJ): %.o: %.s
	$(CC) -c $< -o $@

$(CRT): src/arch/aarch64/syscall.o
	cp $< $(CRT)

$(LIB): $(OBJ)
	ar rcs $@ $^
	@echo ""
	@echo "  Built:  $(LIB)  (AArch64 freestanding)"
	@echo "  CRT:    $(CRT)"
	@echo ""
	@echo "  Link your program with:"
	@echo "    $(CC) -ffreestanding -nostdlib -nostdinc $(LINKFLAGS) \\"
	@echo "          -Iinclude your_program.c $(CRT) $(LIB) -o your_program"

test: $(LIB) $(CRT)
	$(CC) -ffreestanding -nostdlib -nostdinc $(LINKFLAGS) \
		-Iinclude tests/test_all.c $(CRT) $(LIB) -o tests/test_all
	@echo ""
	@./tests/test_all
	@echo ""

demo: $(LIB) $(CRT)
	$(CC) -ffreestanding -nostdlib -nostdinc $(LINKFLAGS) \
		-Iinclude examples/hello.c $(CRT) $(LIB) -o examples/hello
	@echo ""
	@./examples/hello

build: $(LIB) $(CRT)
	$(CC) -ffreestanding -nostdlib -nostdinc $(LINKFLAGS) \
		-Iinclude $(SRC) $(CRT) $(LIB) -o $(OUT)

clean:
	rm -f $(OBJ) $(LIB) $(CRT) tests/test_all examples/hello
	@echo "Cleaned."
