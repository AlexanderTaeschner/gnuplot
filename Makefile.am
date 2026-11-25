## Process this file with automake to produce Makefile.in -*-Makefile-*-
AUTOMAKE_OPTIONS = foreign
AM_DISTCHECK_DVI_TARGET =

SUBDIRS = config m4 term src docs man demo share

EXTRA_DIST = BUGS Copyright FAQ.pdf INSTALL INSTALL.gnu \
PATCHLEVEL PGPKEYS README README.security RELEASE_NOTES \
win

DISTCLEANFILES = stamp-h

ACLOCAL_AMFLAGS = -I m4

distclean-local:
	rm -rf autom4te.cache

dist-hook:
	@for dir in `find $(distdir) -type d -name CVS -print`; do \
	  rm -rf $$dir; done
	@find $(distdir) -type d -name .deps -print | xargs rm -rf
	@find $(distdir) -type f -name .cvsignore -print | xargs rm -f

gnuplot:
	cd src; make binonly

