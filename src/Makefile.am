INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(CAIRO_CFLAGS)					\
	$(PANGO_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DEGG_LOG_FILE=\""$(ACB_LOG_DIR)/acb"\"		\
	-DEGG_VERBOSE="\"ACB_VERBOSE\""			\
	-DEGG_LOGGING="\"ACB_LOGGING\""			\
	-DEGG_CONSOLE="\"ACB_CONSOLE\""

bin_PROGRAMS =						\
	autocodebuild

autocodebuild_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	acb-project.c					\
	acb-project.h					\
	acb-main.c

autocodebuild_LDADD =					\
	$(GLIB_LIBS)

autocodebuild_CFLAGS =					\
	$(WARNINGFLAGS_C)

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in

