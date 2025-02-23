######################### -*- Mode: Makefile-Gmake -*- ########################
## Copyright (C) 2024, Mats Bergstrom
## $Id$
## 
## File name       : Makefile
## Description     : for ShellyControl
## 
## Author          : Mats Bergstrom
## Created On      : Fri Oct  4 08:36:06 2024
## 
## Last Modified By: Mats Bergstrom
## Last Modified On: Fri Oct  4 08:37:17 2024
## Update Count    : 1
###############################################################################




CC		= gcc
CFLAGS		= -Wall -pedantic-errors -g
CPPFLAGS	= -Icfgf
LDLIBS		= -lmosquitto -lcfgf
LDFLAGS		= -Lcfgf

BINDIR		= /usr/local/bin
ETCDIR		= /usr/local/etc
SYSTEMD_DIR 	= /lib/systemd/system

BINARIES 	= shellyctrl
SYSTEMD_FILES 	= shellyctrl.service
ETC_FILES 	= shellyctrl.cfg

CFGFGIT		= https://github.com/mats-bergstrom/cfgf.git


all: cfgf shellyctrl

shellyctrl: shellyctrl.o

shellyctrl.o: shellyctrl.c



.PHONY: cfgf clean uninstall install

cfgf:
	if [ ! -d cfgf ] ; then git clone $(CFGFGIT) ; fi
	cd cfgf && make

really-clean:
	if [ -d cfgf ] ; then rm -rf cfgf ; fi

clean:
	rm -f *.o shellyctrl *~ *.log .*~
	if [ -d cfgf ] ; then cd cfgf && make clean ; fi

uninstall:
	cd $(SYSTEMD_DIR); rm $(SYSTEMD_FILES)
	cd $(BINDIR); rm $(BINARIES)
	cd $(ETCDIR); rm $(ETC_FILES)

install:
	if [ ! -d $(BINDIR) ] ; then mkdir $(BINDIR); fi
	if [ ! -d $(ETCDIR) ] ; then mkdir $(ETCDIR); fi
	cp $(BINARIES) $(BINDIR)
	cp $(ETC_FILES) $(ETCDIR)
	cp $(SYSTEMD_FILES) $(SYSTEMD_DIR)
