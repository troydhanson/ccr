SRCS=$(wildcard test*.c)
OBJS=$(patsubst %.c,%.o,$(SRCS))
PROGS=$(patsubst %.o,%,$(OBJS))

CFLAGS = -I.. -I../../libut/include
CFLAGS += -g -O0
CFLAGS += -Wall -Wextra

ifeq ($(OPT),1)
EXTRA_LDFLAGS = -L/opt/lib
endif

LDFLAGS = -L.. -lcc -L../../lib/libut_build -lut
LDFLAGS += $(EXTRA_LDFLAGS) -ljansson

TEST_TARGET=run_tests
TESTS=./do_tests

all: $(OBJS) $(PROGS) $(TEST_TARGET) 

# static pattern rule: multiple targets 

$(OBJS): %.o: %.c ../libcc.la
	$(CC) -o $@ -c $(CFLAGS) $< 


# use libtool to link the tests. the tests link with
# libcc.la and libut.la, both libtool (pre-installed)
# libraries. (in fact they never get installed). only
# libtool can properly link .la libs to an executable.
$(PROGS): %: %.o
	libtool --mode=link --tag=CC $(CC) -o $@ $< $(LDFLAGS)

run_tests: $(PROGS)
	perl $(TESTS)

.PHONY: clean

clean:	
	rm -f $(PROGS) $(OBJS) *.o test*.out 
