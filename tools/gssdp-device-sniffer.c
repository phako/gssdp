#include <libgssdp/gssdp.h>
#include <libgssdp/gssdp-client-private.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <string.h>

#define GLADE_FILE "gssdp-device-sniffer.glade"

GladeXML *glade_xml;
GSSDPResourceBrowser *resource_browser;
GSSDPClient *client;

void
on_enable_packet_capture_activate (GtkMenuItem *menuitem, gpointer user_data)
{
}

static void
clear_packet_treeview ()
{
        GtkWidget *treeview;
        GtkTreeModel *model;
        GtkTreeIter iter;
        gboolean more;
        time_t *arrival_time;

        treeview = glade_xml_get_widget (glade_xml, "packet-treeview");
        g_assert (treeview != NULL);
        model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
        more = gtk_tree_model_get_iter_first (model, &iter);

        while (more) {
                gtk_tree_model_get (model,
                                &iter, 
                                5, &arrival_time, -1);
                g_free (arrival_time);
                more = gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
        }
}

void
on_details_activate (GtkWidget *scrolled_window, GtkCheckMenuItem *menuitem)
{
        gboolean active;

        active = gtk_check_menu_item_get_active (menuitem);
        g_object_set (G_OBJECT (scrolled_window), "visible", active, NULL);
}

static void
packet_header_to_string (char *header_name,
                GSList *header_val,
                GString **text)
{
        GSList *node;

        g_string_append_printf (*text, "%s: %s",
                        header_name,
                        (char *) header_val->data);

        for (node = header_val->next; node; node = node->next) {
                g_string_append_printf (*text, "; %s", (char *) node->data);
        }
        g_string_append (*text, "\n");
}

static void
clear_textbuffer (GtkTextBuffer *textbuffer)
{
        GtkTextIter start, end;

        gtk_text_buffer_get_bounds (textbuffer, &start, &end);
        gtk_text_buffer_delete (textbuffer, &start, &end);
}

static void
update_packet_details (char *text, unsigned int len)
{
        GtkWidget *textview;
        GtkTextBuffer *textbuffer;
        
        textview = glade_xml_get_widget (glade_xml, "packet-details-textview");
        g_assert (textview != NULL);
        textbuffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
        
        clear_textbuffer (textbuffer);
        gtk_text_buffer_insert_at_cursor (textbuffer, text, len);
}

static void
display_packet (time_t arrival_time, GHashTable *packet_headers)
{
        GString *text;
       
        text = g_string_new ("");
        g_string_printf (text, "Received on: %s\nHeaders:\n\n",
                        ctime (&arrival_time));
        
        g_hash_table_foreach (packet_headers,
                        (GHFunc) packet_header_to_string,
                        &text);

        update_packet_details (text->str, text->len);
        g_string_free (text, TRUE);
}

static void
on_packet_selected (GtkTreeSelection *selection, gpointer user_data)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        time_t *arrival_time;

        if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
                GHashTable *packet_headers;

                gtk_tree_model_get (model,
                                &iter, 
                                4, &packet_headers,
                                5, &arrival_time, -1);
                display_packet (*arrival_time, packet_headers);
                g_boxed_free (G_TYPE_HASH_TABLE, packet_headers);
        }

        else
                update_packet_details ("", 0);
}

void
on_clear_packet_capture_activate (GtkMenuItem *menuitem, gpointer user_data)
{
        clear_packet_treeview ();
}

static char *message_types[] = {"M-SEARCH", "RESPONSE", "NOTIFY"};

static char **
packet_to_treeview_data (const gchar *from_ip,
                time_t arrival_time,
                _GSSDPMessageType type,
                GHashTable *headers)
{
        char **packet_data;
        struct tm *tm;
        GSList *node;

        packet_data = g_malloc (sizeof (char *) * 5);

        /* Set the Time */
        tm = localtime (&arrival_time);
        packet_data[0] = g_strdup_printf ("%02d:%02d", tm->tm_hour, tm->tm_min);

        /* Now the Source Address */
        packet_data[1] = g_strdup (from_ip);
        
        /* Now the Packet Type */
        packet_data[2] = g_strdup (message_types[type]);
        
        /* Now the Packet Information */
        if (type == _GSSDP_DISCOVERY_REQUEST)
                node = g_hash_table_lookup (headers, "ST");
        else
                node = g_hash_table_lookup (headers, "NT");
        
        if (node)
                packet_data[3] = g_strdup (node->data);
        packet_data[4] = NULL;

        return packet_data;
}

static void
append_packet (const gchar *from_ip,
               time_t arrival_time,
               _GSSDPMessageType type,
               GHashTable *headers)
{
        GtkWidget *treeview;
        GtkListStore *liststore;
        GtkTreeIter iter;
        char **packet_data;
        
        treeview = glade_xml_get_widget (glade_xml, "packet-treeview");
        g_assert (treeview != NULL);
        liststore = GTK_LIST_STORE (
                        gtk_tree_view_get_model (GTK_TREE_VIEW (treeview)));
        g_assert (liststore != NULL);
       
        packet_data = packet_to_treeview_data (from_ip,
                        arrival_time,
                        type,
                        headers);
        gtk_list_store_insert_with_values (liststore, &iter, 0,
                        0, packet_data[0],
                        1, packet_data[1],
                        2, packet_data[2],
                        3, packet_data[3],
                        4, headers,
                        5, g_memdup (&arrival_time, sizeof (time_t)),
                        -1);
        g_strfreev (packet_data);
}

static void
on_ssdp_message (GSSDPClient *client,
                const gchar *from_ip,
                _GSSDPMessageType type,
                GHashTable *headers,
                gpointer user_data)
{
        time_t arrival_time;
        
        arrival_time = time (NULL);
        if (type != _GSSDP_DISCOVERY_RESPONSE)
                append_packet (from_ip, arrival_time, type, headers);
}

static void
append_device (const char *uuid,
               const char *last_notify,
               const char *device_type,
               const char *location)
{
        GtkWidget *treeview;
        GtkListStore *liststore;
        GtkTreeIter iter;
       
        treeview = glade_xml_get_widget (glade_xml, "device-details-treeview");
        g_assert (treeview != NULL);
        liststore = GTK_LIST_STORE (
                        gtk_tree_view_get_model (GTK_TREE_VIEW (treeview)));
        g_assert (liststore != NULL);
       
        gtk_list_store_insert_with_values (liststore, &iter, 0,
                        0, uuid,
                        1, (guint64) 1,
                        2, last_notify,
                        3, device_type,
                        4, location,
                        -1);
}

static gboolean 
find_device (GtkTreeModel *model, const char *uuid, GtkTreeIter *iter)
{
        gboolean found = FALSE;
        gboolean more;
       
        more = gtk_tree_model_get_iter_first (model, iter);
        while (more) {
                char *device_uuid;
                gtk_tree_model_get (model,
                                iter, 
                                0, &device_uuid, -1);
                if (device_uuid && strcmp (device_uuid, uuid) == 0) {
                        found = TRUE;
                        break;
                }

                g_free (device_uuid);
                more = gtk_tree_model_iter_next (model, iter);
        }

        return found;
}

static void
update_device (const char *uuid,
               const char *last_notify)
{
        GtkWidget *treeview;
        GtkTreeModel *model;
        GtkTreeIter iter;
       
        treeview = glade_xml_get_widget (glade_xml, "device-details-treeview");
        g_assert (treeview != NULL);
        model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
        g_assert (model != NULL);
      
        if (find_device (model, uuid, &iter)) {
                gint64 notify;

                gtk_tree_model_get (model, &iter, 1, (gint64 *) &notify, -1);
                gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                                1, (gint64) notify+1,
                                2, last_notify,
                                -1);
        }
}

static void
resource_available_cb (GSSDPResourceBrowser *resource_browser,
                       const char           *usn,
                       GList                *locations)
{

        char **usn_tokens;
        char *uuid;

        usn_tokens = g_strsplit (usn, "::", -1);
        g_assert (usn_tokens != NULL && usn_tokens[0] != NULL);

        uuid = usn_tokens[0] + 5; /* skip the prefix 'uuid:' */

        if (usn_tokens[1]) {
                char **urn_tokens;
                time_t current_time;
                struct tm *tm;
                char *last_notify;

                urn_tokens = g_strsplit (usn_tokens[1], ":device:", -1);
                        
                current_time = time (NULL);
                tm = localtime (&current_time);
                last_notify = g_strdup_printf ("%02d:%02d",
                                tm->tm_hour, tm->tm_min);

                if (urn_tokens[1]) {
                        /* Device Announcement */
                        append_device (uuid,
                                       last_notify,
                                       urn_tokens[1],
                                       (char *) locations->data);
                }
                
                else {
                        /* FIXME: not all notifications are getting
                         * counted using this logic
                         */
                        update_device (uuid, last_notify);
                }

                g_free (last_notify);
                g_strfreev (urn_tokens);
        }
                
        g_strfreev (usn_tokens);
}

static void
remove_device (const char *uuid)
{
        GtkWidget *treeview;
        GtkTreeModel *model;
        GtkTreeIter iter;
       
        treeview = glade_xml_get_widget (glade_xml, "device-details-treeview");
        g_assert (treeview != NULL);
        model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
        g_assert (model != NULL);
      
        if (find_device (model, uuid, &iter)) {
                gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
        }
}

static void
resource_unavailable_cb (GSSDPResourceBrowser *resource_browser,
                         const char           *usn)
{
        char **usn_tokens;
        char *uuid;

        usn_tokens = g_strsplit (usn, "::", -1);
        g_assert (usn_tokens != NULL && usn_tokens[0] != NULL);
        uuid = usn_tokens[0] + 5; /* skip the prefix 'uuid:' */
        
        remove_device (uuid);
        
        g_strfreev (usn_tokens);
}

void
on_custom_search_dialog_response (GtkDialog *dialog,
                gint       response,
                gpointer   user_data)
{
        GtkWidget *entry;

        entry = glade_xml_get_widget (glade_xml, "search-target-entry");
        g_assert (entry != NULL);
        gtk_widget_hide (GTK_WIDGET (dialog));
        if (response == GTK_RESPONSE_OK) {
                g_print ("search target: %s\n",
                                gtk_entry_get_text (GTK_ENTRY (entry)));
        }
        gtk_entry_set_text (GTK_ENTRY (entry), "");
}

static GtkTreeModel *
create_packet_treemodel (void)
{
        GtkListStore *store;

        store = gtk_list_store_new (6,
                        G_TYPE_STRING,
                        G_TYPE_STRING,
                        G_TYPE_STRING,
                        G_TYPE_STRING,
                        G_TYPE_HASH_TABLE,
                        G_TYPE_POINTER);

        return GTK_TREE_MODEL (store);
}

static GtkTreeModel *
create_device_treemodel (void)
{
        GtkListStore *store;

        store = gtk_list_store_new (5,
                        G_TYPE_STRING,
                        G_TYPE_INT64,
                        G_TYPE_STRING,
                        G_TYPE_STRING,
                        G_TYPE_STRING);

        return GTK_TREE_MODEL (store);
}

static void
setup_treeview (GtkWidget *treeview,
                GtkTreeModel *model,
                char *headers[])
{
        int i;

        /* Set-up columns */
        for (i=0; headers[i] != NULL; i++) {
                GtkCellRenderer *renderer;
               
                renderer = gtk_cell_renderer_text_new ();
                gtk_tree_view_insert_column_with_attributes (
                                GTK_TREE_VIEW (treeview),
                                -1,
                                headers[i],
                                renderer,
                                "text", i,
                                NULL);
        }

        gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
                        model);
}

static void
setup_treeviews ()
{
        GtkWidget *treeviews[2];
        GtkTreeModel *treemodels[2];
        char *headers[2][6] = { {"Time",
                "Source Address",
                "Packet Type",
                "Packet Information",
                NULL }, {"Unique Identifier",
                "Notify",
                "Last Notify",
                "Device Type",
                "Location",
                NULL } }; 
        GtkTreeSelection *selection;
        int i;

        treeviews[0] = glade_xml_get_widget (glade_xml,
                        "packet-treeview");
        treeviews[1] = glade_xml_get_widget (glade_xml, 
                        "device-details-treeview");
        treemodels[0] = create_packet_treemodel ();
        treemodels[1] = create_device_treemodel ();

        g_assert (treeviews[0] != NULL && treeviews[1] != NULL);

        for (i=0; i<2; i++)
                setup_treeview (treeviews[i], treemodels[i], headers[i]);
        
        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeviews[0]));
        g_assert (selection != NULL);
        g_signal_connect (selection,
                        "changed",
                        G_CALLBACK (on_packet_selected),
                        (gpointer *) treeviews[0]);
}

gboolean
on_delete_event (GtkWidget *widget,
                GdkEvent  *event,
                gpointer   user_data)
{
        gtk_main_quit ();
        return TRUE;
}

static gboolean
init_ui (gint *argc, gchar **argv[])
{
        GtkWidget *main_window;
        
        gtk_init (argc, argv);
        glade_init ();

        /* Try to fetch the glade file from the CWD first */
        glade_xml = glade_xml_new (GLADE_FILE, NULL, NULL); 
        if (glade_xml == NULL) {
                /* Then Try to fetch it from the system path */
                glade_xml = glade_xml_new (UI_DIR "/" GLADE_FILE, NULL, NULL);
                if (glade_xml == NULL) {
                        g_error ("Unable to load the GUI file %s", GLADE_FILE);
                        return FALSE;
                }
        }

        main_window = glade_xml_get_widget (glade_xml, "main-window");
        g_assert (main_window != NULL);

        glade_xml_signal_autoconnect (glade_xml);
        setup_treeviews ();
        gtk_widget_show_all (main_window);

        return TRUE;
}

static void
deinit_ui (void)
{
        g_object_unref (glade_xml);
}

static gboolean
init_upnp (void)
{
        GError *error;
        
        g_thread_init (NULL);

        error = NULL;
        client = gssdp_client_new (NULL, &error);
        if (error) {
                g_critical (error->message);
                g_error_free (error);
                return 1;
        }

        resource_browser = gssdp_resource_browser_new (client,
                                                       GSSDP_ALL_RESOURCES);
        
        g_signal_connect (client,
                          "message-received",
                          G_CALLBACK (on_ssdp_message),
                          NULL);
        g_signal_connect (resource_browser,
                          "resource-available",
                          G_CALLBACK (resource_available_cb),
                          NULL);
        g_signal_connect (resource_browser,
                          "resource-unavailable",
                          G_CALLBACK (resource_unavailable_cb),
                          NULL);

        return TRUE;
}

static void
deinit_upnp (void)
{
        g_object_unref (resource_browser);
        g_object_unref (client);
}

gint
main (gint argc, gchar *argv[])
{
        if (!init_ui (&argc, &argv)) {
           return -2;
        }

        if (!init_upnp ()) {
           return -3;
        }
        
        gtk_main ();
        
        deinit_upnp ();
        deinit_ui ();
        
        return 0;
}
