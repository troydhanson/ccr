srcdir = @srcdir@

# build these external libraries as convenience libs

noinst_LTLIBRARIES = libut.la

libut_la_CFLAGS = -Wall -Wextra
libut_la_CPPFLAGS = -I$(srcdir)/libut/include
libut_la_SOURCES = libut/src/libut.c    \
                   libut/src/utvector.c \
                   libut/src/utmm.c     \
                   libut/src/ringbuf.c
