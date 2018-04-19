CFLAGS = -O3 -I../include -I../../../common
LLDFLAGS = p2p.a -lm -luuid -lrt -lpthread -lstdc++
CC=gcc

.SUFFIXES: .o .cpp .cxx .c
OBJS = p2pdemo.o getopt.o ../../../common/tm.o
	
all: p2pdemo

p2pdemo: $(OBJS)
	$(CC) -o p2pdemo $(OBJS) $(LLDFLAGS) -o $@
    
.cpp.o:
	$(CC) -c $(CFLAGS) $< -o $@
       
.cxx.o:
	$(CC) -c $(CFLAGS) $< -o $@

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm *.o
	rm p2pdemo
