SRCDIR = src
BUILDDIR = build
OUTPUTEXE = querymanager

CC = gcc
CXX = g++
CFLAGS = -m64 -fno-strict-aliasing -pedantic -Wno-unused-parameter -Wall -Wextra -pthread
CXXFLAGS = $(CFLAGS) --std=c++11
LFLAGS = -Wl,-t

DEBUG ?= 0
ifneq ($(DEBUG), 0)
	CFLAGS += -g -O0
else
	CFLAGS += -O2
endif

$(BUILDDIR)/$(OUTPUTEXE): $(BUILDDIR)/connections.obj $(BUILDDIR)/database.obj $(BUILDDIR)/querymanager.obj $(BUILDDIR)/sqlite3.obj
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LFLAGS)

$(BUILDDIR)/connections.obj: $(SRCDIR)/connections.cc $(SRCDIR)/querymanager.hh
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/database.obj: $(SRCDIR)/database.cc $(SRCDIR)/querymanager.hh
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/querymanager.obj: $(SRCDIR)/querymanager.cc $(SRCDIR)/querymanager.hh
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILDDIR)/sqlite3.obj: $(SRCDIR)/sqlite3.c $(SRCDIR)/sqlite3.h
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean

clean:
	@rm -rf $(BUILDDIR)

