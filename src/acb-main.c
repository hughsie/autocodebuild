#include <glib-object.h>

#include "acb-project.h"
#include "acb-common.h"

#include "egg-debug.h"

static gboolean
acb_main_process_project_name (const gchar *subpath, const gchar *folder, gboolean clean, gboolean update, gboolean build)
{
	gchar *path = NULL;
	gboolean ret = TRUE;
	GError *error = NULL;
	AcbProject *project = NULL;

	path = g_build_filename (subpath, folder, NULL);

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

/**
 * acb_main_ensure_has_path:
 **/
static gboolean
acb_main_ensure_has_path (const gchar *path)
{
	gint retval;
	gchar *folder;

	/* create leading path */
	folder = g_path_get_dirname (path);
	retval = g_mkdir_with_parents (folder, 0777);
	g_free (folder);
	return (retval == 0);
}

static gchar *
acb_main_get_code_dir ()
{
	GError *error = NULL;
	GKeyFile *file = NULL;
	gchar *config_file = NULL;
	gchar *data = NULL;
	gchar *code_dir = NULL;

	/* find the file, else create it */
	config_file = g_build_filename (g_get_user_config_dir (), "autocodebuild", "defaults.conf", NULL);
	if (!g_file_test (config_file, G_FILE_TEST_EXISTS)) {

		/* create a good default */
		code_dir = g_build_filename (g_get_home_dir (), "Code", NULL);
		data = g_strdup_printf ("[defaults]\nCodeDirectory=%s\n", code_dir);
		acb_main_ensure_has_path (config_file);

		/* save to the file */
		g_file_set_contents (config_file, data, -1, NULL);
		goto out;
	}

	/* load from file */
	file = g_key_file_new ();
	g_key_file_load_from_file (file, config_file, G_KEY_FILE_NONE, &error);
	code_dir = g_key_file_get_string (file, "defaults", "CodeDirectory", NULL);
	if (code_dir == NULL) {
		egg_error ("cannot load: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (data);
	g_free (config_file);
	if (file != NULL)
		g_key_file_free (file);
	return code_dir;
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
	gchar *code_path = NULL;
	guint i;
	const gchar *filename;
	gchar *dirname = NULL;
	gchar *basename = NULL;

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

	/* get the code location */
	code_path = acb_main_get_code_dir ();

	/* didn't specify any options */
	if (files == NULL && !clean && !update && !build) {
		g_print ("%s\n", options_help);
		goto out;
	}

	/* process the list */
	if (files != NULL) {
		for (i=0; files[i] != NULL; i++) {
			dirname = g_path_get_dirname (files[i]);
			basename = g_path_get_basename (files[i]);
			if (g_strcmp0 (dirname, ".") == 0) {
				/* we didn't specificy a directory */
				acb_main_process_project_name (code_path, basename, clean, update, build);
			} else {
				/* we specified the full directory */
				acb_main_process_project_name (dirname, basename, clean, update, build);
			}
		}
	} else {
		/* try to open */
		dir = g_dir_open (code_path, 0, &error);
		if (dir == NULL) {
			egg_warning ("cannot open directory: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* find each */
		while ((filename = g_dir_read_name (dir)))
			acb_main_process_project_name (code_path, filename, clean, update, build);
		g_dir_close (dir);
	}
out:
	g_free (dirname);
	g_free (basename);
	g_free (code_path);
	g_free (options_help);
	g_strfreev (files);
	return 0;
}

