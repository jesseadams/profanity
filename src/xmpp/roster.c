/*
 * roster.c
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

#include <assert.h>
#include <string.h>

#include <glib.h>
#include <strophe.h>

#include "log.h"
#include "tools/autocomplete.h"
#include "xmpp/connection.h"
#include "xmpp/roster.h"
#include "xmpp/stanza.h"
#include "xmpp/xmpp.h"

#define HANDLE(type, func) xmpp_handler_add(conn, func, XMPP_NS_ROSTER, STANZA_NAME_IQ, type, ctx)

static int _roster_handle_set(xmpp_conn_t * const conn,
    xmpp_stanza_t * const stanza, void * const userdata);
static int _roster_handle_result(xmpp_conn_t * const conn,
    xmpp_stanza_t * const stanza, void * const userdata);

static Autocomplete handle_ac;
static Autocomplete jid_ac;
static Autocomplete resource_ac;
static GHashTable *contacts;
static GHashTable *handle_to_jid;

static gboolean _key_equals(void *key1, void *key2);
static gboolean _datetimes_equal(GDateTime *dt1, GDateTime *dt2);

void
roster_add_handlers(void)
{
    xmpp_conn_t * const conn = connection_get_conn();
    xmpp_ctx_t * const ctx = connection_get_ctx();
    HANDLE(STANZA_TYPE_SET,    _roster_handle_set);
    HANDLE(STANZA_TYPE_RESULT, _roster_handle_result);
}

void
roster_request(void)
{
    xmpp_conn_t * const conn = connection_get_conn();
    xmpp_ctx_t * const ctx = connection_get_ctx();
    xmpp_stanza_t *iq = stanza_create_roster_iq(ctx);
    xmpp_send(conn, iq);
    xmpp_stanza_release(iq);
}

char *
roster_jid_from_handle(const char * const handle)
{
    return g_hash_table_lookup(handle_to_jid, handle);
}

static int
_roster_handle_set(xmpp_conn_t * const conn, xmpp_stanza_t * const stanza,
    void * const userdata)
{
    xmpp_stanza_t *query = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_QUERY);
    xmpp_stanza_t *item =
        xmpp_stanza_get_child_by_name(query, STANZA_NAME_ITEM);

    if (item == NULL) {
        return 1;
    }

    const char *jid = xmpp_stanza_get_attribute(item, STANZA_ATTR_JID);
    const char *name = xmpp_stanza_get_attribute(item, STANZA_ATTR_NAME);
    const char *sub = xmpp_stanza_get_attribute(item, STANZA_ATTR_SUBSCRIPTION);
    if (g_strcmp0(sub, "remove") == 0) {
        autocomplete_remove(jid_ac, jid);
        autocomplete_remove(handle_ac, name);
        g_hash_table_remove(handle_to_jid, name);
        g_hash_table_remove(contacts, jid);
        return 1;
    }

    gboolean pending_out = FALSE;
    const char *ask = xmpp_stanza_get_attribute(item, STANZA_ATTR_ASK);
    if ((ask != NULL) && (strcmp(ask, "subscribe") == 0)) {
        pending_out = TRUE;
    }

    roster_update(jid, name, sub, pending_out);

    return 1;
}

static int
_roster_handle_result(xmpp_conn_t * const conn, xmpp_stanza_t * const stanza,
    void * const userdata)
{
    const char *id = xmpp_stanza_get_attribute(stanza, STANZA_ATTR_ID);

    // handle initial roster response
    if (g_strcmp0(id, "roster") == 0) {
        xmpp_stanza_t *query = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_QUERY);
        xmpp_stanza_t *item = xmpp_stanza_get_children(query);

        while (item != NULL) {
            const char *barejid = xmpp_stanza_get_attribute(item, STANZA_ATTR_JID);
            const char *name = xmpp_stanza_get_attribute(item, STANZA_ATTR_NAME);
            const char *sub = xmpp_stanza_get_attribute(item, STANZA_ATTR_SUBSCRIPTION);

            gboolean pending_out = FALSE;
            const char *ask = xmpp_stanza_get_attribute(item, STANZA_ATTR_ASK);
            if (g_strcmp0(ask, "subscribe") == 0) {
                pending_out = TRUE;
            }

            gboolean added = roster_add(barejid, name, sub, NULL, pending_out);

            if (!added) {
                log_warning("Attempt to add contact twice: %s", barejid);
            }

            item = xmpp_stanza_get_next(item);
        }

        contact_presence_t conn_presence = accounts_get_login_presence(jabber_get_account_name());
        presence_update(conn_presence, NULL, 0);
    }

    return 1;
}

void
roster_init(void)
{
    handle_ac = autocomplete_new();
    jid_ac = autocomplete_new();
    resource_ac = autocomplete_new();
    contacts = g_hash_table_new_full(g_str_hash, (GEqualFunc)_key_equals, g_free,
        (GDestroyNotify)p_contact_free);
    handle_to_jid = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

void
roster_clear(void)
{
    autocomplete_clear(handle_ac);
    autocomplete_clear(jid_ac);
    autocomplete_clear(resource_ac);
    g_hash_table_destroy(contacts);
    contacts = g_hash_table_new_full(g_str_hash, (GEqualFunc)_key_equals, g_free,
        (GDestroyNotify)p_contact_free);
    g_hash_table_destroy(handle_to_jid);
    handle_to_jid = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

void
roster_free()
{
    autocomplete_free(handle_ac);
    autocomplete_free(jid_ac);
    autocomplete_free(resource_ac);
}

void
roster_reset_search_attempts(void)
{
    autocomplete_reset(handle_ac);
    autocomplete_reset(jid_ac);
    autocomplete_reset(resource_ac);
}

gboolean
roster_add(const char * const barejid, const char * const name,
    const char * const subscription, const char * const offline_message,
    gboolean pending_out)
{
    gboolean added = FALSE;
    PContact contact = g_hash_table_lookup(contacts, barejid);

    if (contact == NULL) {
        contact = p_contact_new(barejid, name, subscription, offline_message,
            pending_out);
        g_hash_table_insert(contacts, strdup(barejid), contact);
        if (name != NULL) {
            autocomplete_add(handle_ac, strdup(name));
            g_hash_table_insert(handle_to_jid, strdup(name), strdup(barejid));
        } else {
            autocomplete_add(handle_ac, strdup(barejid));
            g_hash_table_insert(handle_to_jid, strdup(barejid), strdup(barejid));
        }
        autocomplete_add(jid_ac, strdup(barejid));

        added = TRUE;
    }

    return added;
}

gboolean
roster_update_presence(const char * const barejid, Resource *resource,
    GDateTime *last_activity)
{
    assert(barejid != NULL);
    assert(resource != NULL);

    PContact contact = g_hash_table_lookup(contacts, barejid);
    if (contact == NULL) {
        return FALSE;
    }

    if (!_datetimes_equal(p_contact_last_activity(contact), last_activity)) {
        p_contact_set_last_activity(contact, last_activity);
    }
    p_contact_set_presence(contact, resource);
    Jid *jid = jid_create_from_bare_and_resource(barejid, resource->name);
    autocomplete_add(resource_ac, strdup(jid->fulljid));
    jid_destroy(jid);

    return TRUE;
}

gboolean
roster_contact_offline(const char * const barejid,
    const char * const resource, const char * const status)
{
    PContact contact = g_hash_table_lookup(contacts, barejid);
    if (contact == NULL) {
        return FALSE;
    }
    if (resource == NULL) {
        return TRUE;
    } else {
        gboolean result = p_contact_remove_resource(contact, resource);
        if (result == TRUE) {
            Jid *jid = jid_create_from_bare_and_resource(barejid, resource);
            autocomplete_remove(resource_ac, jid->fulljid);
            jid_destroy(jid);
        }

        return result;
    }
}

void
roster_change_handle(const char * const barejid, const char * const new_handle)
{
    PContact contact = g_hash_table_lookup(contacts, barejid);
    const char * current_handle = NULL;
    if (p_contact_name(contact) != NULL) {
        current_handle = strdup(p_contact_name(contact));
    }

    if (contact != NULL) {
        p_contact_set_name(contact, new_handle);

        // current handle exists already
        if (current_handle != NULL) {
            autocomplete_remove(handle_ac, current_handle);
            g_hash_table_remove(handle_to_jid, current_handle);

            if (new_handle != NULL) {
                autocomplete_add(handle_ac, strdup(new_handle));
                g_hash_table_insert(handle_to_jid, strdup(new_handle), strdup(barejid));
            } else {
                autocomplete_add(handle_ac, strdup(barejid));
                g_hash_table_insert(handle_to_jid, strdup(barejid), strdup(barejid));
            }
        // no current handle
        } else {
            if (new_handle != NULL) {
                autocomplete_remove(handle_ac, barejid);
                g_hash_table_remove(handle_to_jid, barejid);

                autocomplete_add(handle_ac, strdup(new_handle));
                g_hash_table_insert(handle_to_jid, strdup(new_handle), strdup(barejid));
            }
        }

        xmpp_conn_t * const conn = connection_get_conn();
        xmpp_ctx_t * const ctx = connection_get_ctx();
        xmpp_stanza_t *iq = stanza_create_roster_set(ctx, barejid, new_handle);
        xmpp_send(conn, iq);
        xmpp_stanza_release(iq);
    }
}

void
roster_update(const char * const barejid, const char * const name,
    const char * const subscription, gboolean pending_out)
{
    PContact contact = g_hash_table_lookup(contacts, barejid);

    if (contact == NULL) {
        contact = p_contact_new(barejid, name, subscription, NULL, pending_out);
        g_hash_table_insert(contacts, strdup(barejid), contact);
        if (name != NULL) {
            autocomplete_add(handle_ac, strdup(name));
            g_hash_table_insert(handle_to_jid, strdup(name), strdup(barejid));
        } else {
            autocomplete_add(handle_ac, strdup(barejid));
            g_hash_table_insert(handle_to_jid, strdup(barejid), strdup(barejid));
        }
        autocomplete_add(jid_ac, strdup(barejid));
    } else {
        p_contact_set_subscription(contact, subscription);
        p_contact_set_pending_out(contact, pending_out);

        const char * const new_handle = name;
        const char * current_handle = NULL;
        if (p_contact_name(contact) != NULL) {
            current_handle = strdup(p_contact_name(contact));
        }

        p_contact_set_name(contact, new_handle);

        // current handle exists already
        if (current_handle != NULL) {
            autocomplete_remove(handle_ac, current_handle);
            g_hash_table_remove(handle_to_jid, current_handle);

            if (new_handle != NULL) {
                autocomplete_add(handle_ac, strdup(new_handle));
                g_hash_table_insert(handle_to_jid, strdup(new_handle), strdup(barejid));
            } else {
                autocomplete_add(handle_ac, strdup(barejid));
                g_hash_table_insert(handle_to_jid, strdup(barejid), strdup(barejid));
            }
        // no current handle
        } else {
            if (new_handle != NULL) {
                autocomplete_remove(handle_ac, barejid);
                g_hash_table_remove(handle_to_jid, barejid);

                autocomplete_add(handle_ac, strdup(new_handle));
                g_hash_table_insert(handle_to_jid, strdup(new_handle), strdup(barejid));
            }
        }
    }
}

gboolean
roster_has_pending_subscriptions(void)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    g_hash_table_iter_init(&iter, contacts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        PContact contact = (PContact) value;
        if (p_contact_pending_out(contact)) {
            return TRUE;
        }
    }

    return FALSE;
}

GSList *
roster_get_contacts(void)
{
    GSList *result = NULL;
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    g_hash_table_iter_init(&iter, contacts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        result = g_slist_append(result, value);
    }

    // resturn all contact structs
    return result;
}

char *
roster_find_contact(char *search_str)
{
    return autocomplete_complete(handle_ac, search_str);
}

char *
roster_find_jid(char *search_str)
{
    return autocomplete_complete(jid_ac, search_str);
}

char *
roster_find_resource(char *search_str)
{
    return autocomplete_complete(resource_ac, search_str);
}

PContact
roster_get_contact(const char const *barejid)
{
    return g_hash_table_lookup(contacts, barejid);
}

static
gboolean _key_equals(void *key1, void *key2)
{
    gchar *str1 = (gchar *) key1;
    gchar *str2 = (gchar *) key2;

    return (g_strcmp0(str1, str2) == 0);
}

static gboolean
_datetimes_equal(GDateTime *dt1, GDateTime *dt2)
{
    if ((dt1 == NULL) && (dt2 == NULL)) {
        return TRUE;
    } else if ((dt1 == NULL) && (dt2 != NULL)) {
        return FALSE;
    } else if ((dt1 != NULL) && (dt2 == NULL)) {
        return FALSE;
    } else {
        return g_date_time_equal(dt1, dt2);
    }
}