AC_INIT([ccr], 1.0)
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
m4_ifdef([AM_SILENT_RULES],
    [AM_SILENT_RULES([yes])
    ])
LT_INIT([shared])
AC_PROG_CC

have_shr_header=n
have_shr_lib=n
AC_CHECK_HEADERS([shr.h],[have_shr_header=y])
AC_CHECK_LIB(shr,shr_readv,[have_shr_lib=y])
if test "x${have_shr_header}${have_shr_lib}" != xyy
then
  AC_MSG_ERROR([
  -----------------------------------------------------
  The libshr build prerequisite was not found. Please
  see the build instructions, install libshr and retry.
  -----------------------------------------------------
  ])
fi

# is Jansson installed
have_jansson_header=n
have_jansson_lib=n
AC_CHECK_HEADERS([jansson.h],[have_jansson_header=y])
AC_CHECK_LIB(jansson,json_dumps,[have_jansson_lib=y])
if test "x${have_jansson_header}${have_jansson_lib}" != xyy
then
  AC_MSG_ERROR([
  -----------------------------------------------------
  Please install libjansson, then retry.
  -----------------------------------------------------
  ])
fi


# is ncurses installed
AC_CHECK_LIB(ncurses,initscr,
  AM_CONDITIONAL(HAVE_NCURSES,true),
  AM_CONDITIONAL(HAVE_NCURSES,false))

# is nanomsg installed
AC_CHECK_LIB(nanomsg,nn_socket,
  AM_CONDITIONAL(HAVE_NANOMSG,true),
  AM_CONDITIONAL(HAVE_NANOMSG,false))

# is libpcre3 installed
AC_CHECK_LIB(pcre,pcre_exec,
  AM_CONDITIONAL(HAVE_PCRE,true),
  AM_CONDITIONAL(HAVE_PCRE,false))

# is librdkafka installed
AC_CHECK_LIB(rdkafka,rd_kafka_new,
  AM_CONDITIONAL(HAVE_RDKAFKA,true),
  AM_CONDITIONAL(HAVE_RDKAFKA,false))

AC_CONFIG_FILES([Makefile
   lib/Makefile
   lib/libut_build/Makefile
   cc/Makefile
   ccr/Makefile
   ccr/src/Makefile
   ccr/util/Makefile
   ])
AC_OUTPUT
