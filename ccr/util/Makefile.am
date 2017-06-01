srcdir = @srcdir@

bin_PROGRAMS = ccr-tool 
lib_LTLIBRARIES = libmodccr_dummy.la
noinst_HEADERS = sconf.h modccr.h

ccr_tool_SOURCES = ccr-tool.c
ccr_tool_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
ccr_tool_LDADD = -L../src -lccr -L../../lib/libut_build -lut -lshr -ljansson -ldl

libmodccr_dummy_la_SOURCES = modccr-dummy.c sconf.c
libmodccr_dummy_la_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
libmodccr_dummy_la_LIBDADD = -L../src -lccr -lshr -ljansson
libmodccr_dummy_la_LDFLAGS = -version-info 0:0:0

if HAVE_RDKAFKA
lib_LTLIBRARIES += libmodccr_kafka.la 
libmodccr_kafka_la_SOURCES = modccr-kafka.c sconf.c
libmodccr_kafka_la_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
libmodccr_kafka_la_LIBDADD = -L../src -lccr -lshr -ljansson -lrdkafka
libmodccr_kafka_la_LDFLAGS = -version-info 0:0:0
endif
