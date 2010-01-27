#include <glib-object.h>

#include "acb-project.h"
#include "acb-common.h"

#include "egg-debug.h"

static gboolean
acb_main_process_project_name (const gchar *folder, gboolean clean, gboolean update, gboolean build)
{
	gchar *path = NULL;
	gboolean ret = TRUE;
	GError *error = NULL;
	AcbProject *project = NULL;

	path = g_build_filename ("/home/hughsie/Code", folder, NULL);

	/* check exists */
	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		g_print ("%s: %s\n", "Does not exist", path);
		goto out;
	}

	/* check is a directory */
	if (!g_file_test (path, G_FILE_TEST_IS_DIR))
		goto out;

	/* operate on folder */
	project = acb_project_new ();
	acb_project_set_path (project, path);
	if (clean) {
		ret = acb_project_clean (project, &error);
		if (!ret) {
			g_print ("Failed to clean: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
	}
	if (update) {
		ret = acb_project_update (project, &error);
		if (!ret) {
			g_print ("Failed to update: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
	}
	if (build) {
		ret = acb_project_build (project, &error);
		if (!ret) {
			g_print ("Failed to build: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
	}
out:
	if (project != NULL)
		g_object_unref (project);
	g_free (path);
	return ret;
}

int
main (int argc, char **argv)
{
//	gboolean ret;
	GError *error = NULL;
	GDir *dir = NULL;
	gchar *options_help = NULL;
	GOptionContext *context;
	gboolean verbose = FALSE;
	gboolean clean = FALSE;
	gboolean update = FALSE;
	gboolean build = FALSE;
	gchar **files = NULL;
	guint i;
	const gchar *filename;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "clean", 'c', 0, G_OPTION_ARG_NONE, &clean,
			"Clean projects", NULL},
		{ "update", 'u', 0, G_OPTION_ARG_NONE, &update,
			"Update projects", NULL},
		{ "build", 'b', 0, G_OPTION_ARG_NONE, &build,
			"Build projects", NULL},
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &files,
			"Projects", NULL },
		{ NULL}
	};

	context = g_option_context_new ("AutoCodeBuild");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	g_type_init ();

	/* process the list */
	if (files != NULL) {
		for (i=0; files[i] != NULL; i++)
			acb_main_process_project_name (files[i], clean, update, build);
	} else {
		/* try to open */
		dir = g_dir_open ("/home/hughsie/Code", 0, &error);
		if (dir == NULL) {
			egg_warning ("cannot open directory: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* find each */
		while ((filename = g_dir_read_name (dir)))
			acb_main_process_project_name (filename, clean, update, build);
		g_dir_close (dir);
	}
out:
	g_free (options_help);
	g_strfreev (files);
	return 0;
}

