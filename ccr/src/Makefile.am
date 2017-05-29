srcdir = @srcdir@

lib_LTLIBRARIES = libccr.la

libccr_la_CFLAGS = -Wall -Wextra
libccr_la_CPPFLAGS = -I$(srcdir)/../../lib/libut/include -I$(srcdir)/../../cc
libccr_la_LIBADD = ../../cc/libcc.la
libccr_la_SOURCES = ccr.c 
libccr_la_LDFLAGS = -version-info 0:0:0
pkginclude_HEADERS = ccr.h
