ccflags-y += -O3 \
    -DLZ4_FREESTANDING=1 \
    -DLZ4_FAST_DEC_LOOP=1

obj-y += lz4.o lz4hc.o

CFLAGS_lz4.o := $(call cc-option,-Wframe-larger-than=20480)
CFLAGS_lz4hc.o := $(call cc-option,-Wframe-larger-than=70000)
