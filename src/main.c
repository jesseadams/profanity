/*
 * main.c
 *
 * Copyright (C) 2012, 2013 James Booth <boothj5@gmail.com>
 *
 * This file is part of Profanity.
 *
 * Profanity is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Profanity is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Profanity.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <string.h>
#include <glib.h>

#include "config.h"
#ifdef HAVE_GIT_VERSION
#include "gitversion.c"
#endif

#include "profanity.h"

static gboolean disable_tls = FALSE;
static gboolean version = FALSE;
static char *log = "INFO";

int
main(int argc, char **argv)
{
    static GOptionEntry entries[] =
    {
        { "version", 'v', 0, G_OPTION_ARG_NONE, &version, "Show version information", NULL },
        { "disable-tls", 'd', 0, G_OPTION_ARG_NONE, &disable_tls, "Disable TLS", NULL },
        { "log",'l', 0, G_OPTION_ARG_STRING, &log, "Set logging levels, DEBUG, INFO (default), WARN, ERROR", "LEVEL" },
        { NULL }
    };

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("%s\n", error->message);
        g_option_context_free(context);
        return 1;
    }

    g_option_context_free(context);

    if (version == TRUE) {

        if (strcmp(PACKAGE_STATUS, "development") == 0) {
#ifdef HAVE_GIT_VERSION
            g_print("Profanity, version %sdev.%s.%s\n", PACKAGE_VERSION, PROF_GIT_BRANCH, PROF_GIT_REVISION);
#else
            g_print("Profanity, version %sdev\n", PACKAGE_VERSION);
#endif
        } else {
            g_print("Profanity, version %s\n", PACKAGE_VERSION);
        }

        g_print("Copyright (C) 2012, 2013 James Booth <%s>.\n", PACKAGE_BUGREPORT);
        g_print("License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n");
        g_print("\n");
        g_print("This is free software; you are free to change and redistribute it.\n");
        g_print("There is NO WARRANTY, to the extent permitted by law.\n");
        return 0;
    }

    prof_run(disable_tls, log);

    return 0;
}
