#include "xdg.h"
#ifdef USE_XDG_BASEDIR

#include "plot.h"
#include "util.h"
#include "alloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Names of the environment variables, mapped to XDGVarType values */
static const char *xdg_env_vars[] = {
    [kXDGConfigHome] = "XDG_CONFIG_HOME",
    [kXDGDataHome] = "XDG_DATA_HOME",
    [kXDGStateHome] = "XDG_STATE_HOME",
    [kXDGCacheHome] = "XDG_CACHE_HOME",
    [kXDGRuntimeDir] = "XDG_RUNTIME_DIR",
    [kXDGConfigDirs] = "XDG_CONFIG_DIRS",
    [kXDGDataDirs] = "XDG_DATA_DIRS",
};

/* Defaults for XDGVarType values
 * Used in case environment variables contain nothing. Need to be expanded.
 */
static const char *const xdg_defaults[] = {
    [kXDGConfigHome] = "~/.config",
    [kXDGDataHome] = "~/.local/share",
    [kXDGStateHome] = "~/.local/state",
    [kXDGCacheHome] = "~/.cache",
    [kXDGRuntimeDir] = "",
    [kXDGConfigDirs] = "/etc/xdg/",
    [kXDGDataDirs] = "/usr/local/share/:/usr/share/",
};

/* This function does nothing if dirent.h and windows.h not available. */
static TBOOLEAN
existdir(const char *name)
{
#if defined(HAVE_DIRENT)
    DIR *dp;
    if ((dp = opendir(name)) == NULL)
	return FALSE;

    closedir(dp);
    return TRUE;
#else
    int_warn(NO_CARET,
	     "Test on directory existence not supported\n\t('%s!')",
	     name);
    return FALSE;
#endif
}

#if (0)		/* Not used */
#if defined(_MSC_VER)
# include <io.h>		/* for _access */
#endif
static TBOOLEAN
existfile(const char *name)
{
#ifdef _MSC_VER
    return (_access(name, 0) == 0);
#else
    return (access(name, F_OK) == 0);
#endif
}
#endif

/* helper function: return TRUE if dirname exists or can be created */

static TBOOLEAN check_dir(const char *dirname) {
#ifdef HAVE_SYS_STAT_H
     return existdir(dirname) || !mkdir(dirname, 00700);
#else	/* I believe this does not happen */
     return FALSE;
#endif
}

/* name used for subdirectory */
static const char *appname = "gnuplot";

/* Return pathname of XDG base directory or a file in it
 *
 * XDG base directory specification:
 * https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
 *
 * @param[in]  idx     XDG variable to use.
 * @param[in]  fname   if not NULL, name of a file in the base directory
 * @param[in]  subdir  if TRUE, append "/gnuplot" to the base directory
 * @param[in]  create  if TRUE, try to create the directory
 *
 * @return [allocated] pathname of the base directory if fname is NULL,
 *                     else of the file in the directory. Returns NULL if
 *                     create is TURE but the directory can't be created.
 */
char *xdg_get_path(XDGVarType idx, const char* fname,
		    TBOOLEAN subdir, TBOOLEAN create) {
    char *pathname;
    if ((pathname = getenv(xdg_env_vars[idx]))) {
        pathname = gp_strdup(pathname);	/* use the environment variable */
    }
    else {
	pathname = gp_strdup(xdg_defaults[idx]);    /* use the default */
	if (strchr(pathname, '~')) {
	    /* But if we're running anonymously, e.g. from a cgi script
	     * don't try to access or create any private directories.
	     */
	    if (getenv("HOME"))
		gp_expand_tilde(&pathname);
	    else {
		free(pathname);
		return NULL;
	    }
	}
    }
    if (create && !check_dir(pathname)) {
	free(pathname);
	return NULL;
    }
    if (subdir) {
	pathname = gp_realloc(pathname,
			    strlen(pathname) + strlen(appname) + 2, "XDG");
	PATH_CONCAT(pathname, appname);
	if (create && !check_dir(pathname)) {
	    free(pathname);
	    return NULL;
	}
    }
    if (fname) {
	pathname = gp_realloc(pathname,
			    strlen(pathname) + strlen(fname) + 2, "XDG");
	PATH_CONCAT(pathname, fname);
    }
    return pathname;
}

#endif /* USE_XDG_BASEDIR */
