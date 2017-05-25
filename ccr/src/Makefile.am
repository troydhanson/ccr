srcdir = @srcdir@

lib_LIBRARIES = libccr.a

libccr_a_CFLAGS = -Wall -Wextra
libccr_a_CPPFLAGS = -I$(srcdir)/../../lib/libut/include -I$(srcdir)/../../cc
libccr_a_LIBADD = ../../cc/libcc_a-cc.o  ../../cc/libcc_a-cc_types.o ../../lib/libut_build/libut/src/libut_a-libut.o  ../../lib/libut_build/libut/src/libut_a-ringbuf.o  ../../lib/libut_build/libut/src/libut_a-utmm.o  ../../lib/libut_build/libut/src/libut_a-utvector.o
libccr_a_SOURCES = ccr.c 
pkginclude_HEADERS = ccr.h
