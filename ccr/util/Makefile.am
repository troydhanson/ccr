srcdir = @srcdir@

bin_PROGRAMS = ccr-tool ccr-pub-redis
lib_LTLIBRARIES = libmodccr_dummy.la
noinst_HEADERS = sconf.h
noinst_PROGRAMS = ccr-bulkread-template

ccr_tool_SOURCES = ccr-tool.c
ccr_tool_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
ccr_tool_LDADD = -L../src -lccr -L../../lib/libut_build -lut -lshr -ljansson -ldl

ccr_pub_redis_SOURCES = ccr-pub-redis.c
ccr_pub_redis_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
ccr_pub_redis_LDADD = -L../src -lccr -L../../lib/libut_build -lut -lshr -ljansson

libmodccr_dummy_la_SOURCES = modccr-dummy.c sconf.c
libmodccr_dummy_la_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
libmodccr_dummy_la_LIBDADD = -L../src -lccr 
libmodccr_dummy_la_LDFLAGS = -version-info 0:0:0 -lshr -ljansson

if HAVE_RDKAFKA
lib_LTLIBRARIES += libmodccr_kafka.la 
libmodccr_kafka_la_SOURCES = modccr-kafka.c sconf.c
libmodccr_kafka_la_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
libmodccr_kafka_la_LIBDADD = -L../src -lccr 
libmodccr_kafka_la_LDFLAGS = -version-info 0:0:0 -lshr -ljansson -lrdkafka

bin_PROGRAMS += ccr-pub-kafka
ccr_pub_kafka_SOURCES = ccr-pub-kafka.c
ccr_pub_kafka_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
ccr_pub_kafka_LDADD = -L../src -lccr -L../../lib/libut_build -lut -lshr -ljansson -lrdkafka
endif

ccr_bulkread_template_SOURCES = ccr-bulkread-template.c
ccr_bulkread_template_CPPFLAGS = -I$(srcdir)/../src -I$(srcdir)/../../cc -I$(srcdir)/../../lib/libut_build/libut/include
ccr_bulkread_template_LDADD = -L../src -lccr -L../../lib/libut_build -lut -lshr -ljansson 


