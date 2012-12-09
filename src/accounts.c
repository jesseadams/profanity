/*
 * accounts.c
 *
 * Copyright (C) 2012 James Booth <boothj5@gmail.com>
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

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "accounts.h"
#include "files.h"
#include "log.h"
#include "prof_autocomplete.h"

static gchar *accounts_loc;
static GKeyFile *accounts;

static PAutocomplete login_ac;

static void _save_accounts(void);

void
accounts_load(void)
{
    log_info("Loading accounts");
    login_ac = p_autocomplete_new();
    accounts_loc = files_get_accounts_file();

    accounts = g_key_file_new();
    g_key_file_load_from_file(accounts, accounts_loc, G_KEY_FILE_KEEP_COMMENTS,
        NULL);

    // create the logins searchable list for autocompletion
    gsize njids;
    gchar **jids =
        g_key_file_get_groups(accounts, &njids);

    gsize i;
    for (i = 0; i < njids; i++) {
        p_autocomplete_add(login_ac, strdup(jids[i]));
    }

    for (i = 0; i < njids; i++) {
        free(jids[i]);
    }
    free(jids);
}

void
accounts_close(void)
{
    p_autocomplete_clear(login_ac);
    g_key_file_free(accounts);
}

char *
accounts_find_login(char *prefix)
{
    return p_autocomplete_complete(login_ac, prefix);
}

void
accounts_reset_login_search(void)
{
    p_autocomplete_reset(login_ac);
}

void
accounts_add_login(const char *jid, const char *altdomain)
{
    // doesn't yet exist
    if (!g_key_file_has_group(accounts, jid)) {
        g_key_file_set_boolean(accounts, jid, "enabled", TRUE);
        g_key_file_set_string(accounts, jid, "jid", jid);
        if (altdomain != NULL) {
            g_key_file_set_string(accounts, jid, "server", altdomain);
        }

        _save_accounts();

    // already exists, update old style accounts
    } else {
        g_key_file_set_string(accounts, jid, "jid", jid);
        _save_accounts();
    }

}

gchar**
accounts_get_list(void)
{
    return g_key_file_get_groups(accounts, NULL);
}

ProfAccount*
accounts_get_account(const char * const name)
{
    if (!g_key_file_has_group(accounts, name)) {
        return NULL;
    } else {
        ProfAccount *account = malloc(sizeof(ProfAccount));
        account->name = strdup(name);
        gchar *jid = g_key_file_get_string(accounts, name, "jid", NULL);
        if (jid != NULL) {
            account->jid = strdup(jid);
        } else {
            account->jid = strdup(name);
            g_key_file_set_string(accounts, name, "jid", name);
            _save_accounts();
        }
        account->enabled = g_key_file_get_boolean(accounts, name, "enabled", NULL);
        gchar *server = g_key_file_get_string(accounts, name, "server", NULL);
        if (server != NULL) {
            account->server = strdup(server);
        } else {
            account->server = NULL;
        }

        return account;
    }
}

void
accounts_free_account(ProfAccount *account)
{
    if (account != NULL) {
        if (account->name != NULL) {
            free(account->name);
            account->name = NULL;
        }
        if (account->jid != NULL) {
            free(account->jid);
            account->jid = NULL;
        }
        if (account->server != NULL) {
            free(account->server);
            account->server = NULL;
        }
        account = NULL;
    }
}

static void
_save_accounts(void)
{
    gsize g_data_size;
    char *g_accounts_data = g_key_file_to_data(accounts, &g_data_size, NULL);
    g_file_set_contents(accounts_loc, g_accounts_data, g_data_size, NULL);
}
