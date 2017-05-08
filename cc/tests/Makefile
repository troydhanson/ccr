PROGS=test1 test2 test3 test4 test5
OBJS=$(patsubst %,%.o,$(PROGS))

CFLAGS = -I.. -I../../libut/include
CFLAGS += -g -O0
CFLAGS += -Wall -Wextra
LDFLAGS=-L.. -lcc -L../../lib/libut_build -lut -ljansson

TEST_TARGET=run_tests
TESTS=./do_tests

all: $(OBJS) $(PROGS) $(TEST_TARGET) 

# static pattern rule: multiple targets 

$(OBJS): %.o: %.c
	$(CC) -o $@ -c $(CFLAGS) $< 

$(PROGS): %: %.o
	$(CC) -o $@ $(CFLAGS) $< $(LDFLAGS)


run_tests: $(PROGS)
	perl $(TESTS)

.PHONY: clean

clean:	
	rm -f $(PROGS) $(OBJS) *.o test*.out 
