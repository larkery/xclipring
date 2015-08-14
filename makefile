CFLAGS=-std=c99 -Wall -g
LDLIBS=-lxcb -lxcb-xfixes

all: xclipring

xclipring: xclipring.o ring.o x11.o
