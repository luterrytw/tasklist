ACLOCAL_AMFLAGS = -I m4
include_HEADERS = ../inc/tasklist.h
AM_CFLAGS = -g -I../inc -Wall -fPIC -Wl,-rpath,.
lib_LTLIBRARIES = libtasklist.la
libtasklist_la_SOURCES = tasklist.c tcp-socket.c
libtasklist_la_LDFLAGS = -llog -ldl -release 1.0.0
