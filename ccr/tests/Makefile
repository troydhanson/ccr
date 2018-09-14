SRCS=$(wildcard test*.c)
PROGS=$(patsubst %.c,%,  $(SRCS))
OBJS =$(patsubst %.c,%.o,$(SRCS))

CFLAGS = -I../src -I../../cc -I../../lib/libut/include
CFLAGS += -Wall #-Wextra
CFLAGS += -g -O0
#CFLAGS += -O2
LDFLAGS=-lshr -ljansson

STATIC_OBJS=ccr.o cc.o cc_xcpf.o cc_json.o cc_mm.o ../../lib/libut/libut.a

all: $(STATIC_OBJS) $(PROGS) tests

# rather than using libtool which turns our 
# tests into shell scripts, to dynamically
# link the pre-installed ../src/libccr.so,
# this Makefile builds the ../src/ objects
# itself and links them right in statically
$(STATIC_OBJS):
	$(CC) -c $(CFLAGS) ../src/ccr.c
	$(CC) -c $(CFLAGS) ../../cc/cc_xcpf.c
	$(CC) -c $(CFLAGS) ../../cc/cc_json.c
	$(CC) -c $(CFLAGS) ../../cc/cc_mm.c
	$(CC) -c $(CFLAGS) ../../cc/cc.c
	$(MAKE) -C ../../lib/libut -f Makefile.standalone

# static pattern rule: multiple targets 
$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $< 

$(PROGS): %: %.o $(STATIC_OBJS)
	$(CC) -o $@ $(CFLAGS) $< $(STATIC_OBJS) $(LDFLAGS)

.PHONY: clean tests

tests:
	perl ./do_tests

clean:	
	rm -f $(OBJS) $(PROGS) *.out *.ring *.o
