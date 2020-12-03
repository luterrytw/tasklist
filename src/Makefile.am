ACLOCAL_AMFLAGS = -I m4
include_HEADERS = ../inc/tasklist.h
AM_CFLAGS = -g -I../inc -Wall -fPIC -Wl,-rpath,.
lib_LTLIBRARIES = libtasklist.la
libtasklist_la_SOURCES = tasklist.c listutil.c
libtasklist_la_LDFLAGS = -llog -ldl -version-info 1:0:0
