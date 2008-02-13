/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *      Free Software Foundation, Inc.
 *      59 Temple Place - Suite 330
 *      Boston, MA 02111-1307, USA
 *
 *
 * snarf-http.c - this source file contains functions for downloading
 * files via HTTP.  Based on snarf (http://www.xach.com/snarf/) by
 * Zachary Beane <xach@xach.com>, released under the GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <pwd.h>
#include <ctype.h>
#include <unistd.h>

#include "snarf.h"
#include "snarf-internal.h"
#include "user-interface.h"
#include "misc.h"


extern int default_opts;

int redirect_count = 0;
#define REDIRECT_MAX 10

typedef struct _HttpHeader 	HttpHeader;
typedef struct _HttpHeaderEntry HttpHeaderEntry;
typedef struct _List 		List;

struct _HttpHeader {
    List *header_list;
};

struct _HttpHeaderEntry {
    char *key;
    char *value;
};

struct _List {
    void *data;
    List *next;
};



static List *list_new(void)
{
    List *new_list;

    new_list = malloc(sizeof(List));

    new_list->data = NULL;
    new_list->next = NULL;

    return new_list;

} /* list_new() */



static void list_append(List *l, void *data)
{
    if (l->data == NULL) {
        l->data = data;
        return;
    }

    while (l->next) {
        l = l->next;
    }

    l->next = list_new();
    l->next->data = data;
    l->next->next = NULL;

} /* list_append() */



/* written by lauri alanko */
static char *base64(char *bin, int len)
{
    char *buf= (char *)malloc((len+2)/3*4+1);
    int i=0, j=0;

    char BASE64_END = '=';
    char base64_table[64]= "ABCDEFGHIJKLMNOPQRSTUVWXYZ" 
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
	
    while( j < len - 2 ) {
        buf[i++]=base64_table[bin[j]>>2];
        buf[i++]=base64_table[((bin[j]&3)<<4)|(bin[j+1]>>4)];
        buf[i++]=base64_table[((bin[j+1]&15)<<2)|(bin[j+2]>>6)];
        buf[i++]=base64_table[bin[j+2]&63];
        j+=3;
    }

    switch ( len - j ) {
      case 1:
        buf[i++] = base64_table[bin[j]>>2];
        buf[i++] = base64_table[(bin[j]&3)<<4];
        buf[i++] = BASE64_END;
        buf[i++] = BASE64_END;
        break;
      case 2:
        buf[i++] = base64_table[bin[j] >> 2];
        buf[i++] = base64_table[((bin[j] & 3) << 4) 
                                | (bin[j + 1] >> 4)];
        buf[i++] = base64_table[(bin[j + 1] & 15) << 2];
        buf[i++] = BASE64_END;
        break;
      case 0:
        break;
    }
    buf[i]='\0';
    return buf;

} /* base64() */



static HttpHeader *make_http_header(char *r)
{
    HttpHeader *h		= NULL;
    HttpHeaderEntry	*he 	= NULL;
    char *s			= NULL;
    char *raw_header	= NULL;
    char *raw_header_head	= NULL;

    raw_header = strdup(r);
    /* Track this to free at the end */
    raw_header_head = raw_header;
        
    h = malloc(sizeof(HttpHeader));
    h->header_list = list_new();

    /* Skip the first line: "HTTP/1.X NNN Comment\r?\n" */
    s = raw_header;
    while (*s != '\0' && *s != '\r' && *s != '\n') s++;
    
    while (*s != '\0' && isspace(*s)) s++;
    
    raw_header = s;

    s = strstr(raw_header, ": ");
    while (s) {
        /* Set ':' to '\0' to terminate the key */
        *s++ = '\0'; 
        he = malloc(sizeof(HttpHeaderEntry));
        he->key = strdup(raw_header);
        /* Make it lowercase so we can lookup case-insensitive */
        nvstrtolower(he->key);

        /* Now get the value */
        s++;
        raw_header = s;
        while (*s != '\0' && *s != '\r' && *s != '\n') {
            s++;
        }
        *s++ = '\0';
        he->value = strdup(raw_header);
        list_append(h->header_list, he);

        /* Go to the next line */
        while (*s != '\0' && isspace(*s)) {
            s++;
        }
        raw_header = s;
        s = strstr(raw_header, ": ");
    }

    free(raw_header_head);
    return h;

} /* make_http_header() */



static void free_http_header(HttpHeader *h)
{
    List *l;
    List *l1;
    HttpHeaderEntry *he;

    if (h == NULL) return;
    
    l = h->header_list;
    while (l && l->data) {
        he = l->data;
        free(he->key);
        free(he->value);
        free(l->data);
        l = l->next;
    }

    l = h->header_list;
    while (l) {
        l1 = l->next;
        free(l);
        l = l1;
    }
} /* free_http_header() */

              

static char *get_header_value(char *key, HttpHeader *header)
{
    List *l			= NULL;
    HttpHeaderEntry *he	= NULL;

    l = header->header_list;

    while (l && l->data) {
        he = l->data;
        if (strcmp(he->key, key) == 0) {
            return strdup(he->value);
        }
        l = l->next;
    }

    return NULL;

} /* get_header_value() */



static char *get_raw_header(int fd)
{
    char *header = NULL;
    char buf[SNARF_BUFSIZE]; 	/* this whole function is pathetic. please
                                   rewrite it for me. */
    int bytes_read = 0;
    int total_read = 0;

    header = strdup("");

    buf[0] = buf[1] = buf[2] = '\0';

    while( (bytes_read = read(fd, buf, 1)) ) {
        total_read += bytes_read;

        header = nvstrcat(header, buf, NULL);
        if( total_read > 1) {
            if( strcmp(header + (total_read - 2), "\n\n") == 0 )
                break;
        }

        if( total_read > 3 ) {
            if( strcmp(header + (total_read - 4), "\r\n\r\n") 
                == 0 )
                break;
        }
    }

    return header;

} /* get_raw_header() */



static char *get_request(UrlResource *rsrc)
{
    char *request = NULL;
    char *auth = NULL;
    Url *u;
        
    u = rsrc->url;

    request = nvstrcat("GET ", u->path, u->file, " HTTP/1.0\r\n", 
                       "Host: ", u->host, "\r\n", NULL);

    if (u->username && u->password) {
        auth = nvstrcat(u->username, ":", u->password, NULL);
        auth = base64(auth, strlen(auth));
        request = nvstrcat(request, "Authorization: Basic ",
                           auth, "\r\n", NULL);
    }

    if (rsrc->proxy_username && rsrc->proxy_password) {
        auth = nvstrcat(rsrc->proxy_username, ":", 
                        rsrc->proxy_password, NULL);
        auth = base64(auth, strlen(auth));
        request = nvstrcat(request, "Proxy-Authorization: Basic ",
                           auth, "\r\n", NULL);
    }

    request = nvstrcat(request, "User-Agent: ", PROGRAM_NAME, "/",
                       NVIDIA_INSTALLER_VERSION, NULL);

    /* This CRLF pair closes the User-Agent key-value set. */
    request = nvstrcat(request, "\r\n", NULL);

    /* If SNARF_HTTP_REFERER is set, spoof it. */
    if (getenv("SNARF_HTTP_REFERER")) {
        request = nvstrcat(request, "Referer: ",
                           getenv("SNARF_HTTP_REFERER"),
                           "\r\n", NULL);
    }
        
    request = nvstrcat(request, "\r\n", NULL);

    return request;

} /* get_request() */


int http_transfer(UrlResource *rsrc)
{
    Url *u = NULL;
    Url *proxy_url = NULL;
    Url *redir_u = NULL;
    char *request = NULL;
    char *raw_header = NULL;
    HttpHeader *header = NULL;
    char *len_string = NULL;
    char *new_location = NULL;
    char buf[SNARF_BUFSIZE];
    int sock = 0;
    ssize_t bytes_read = 0;
    int retval = FALSE;
    int i;

    /* make sure we haven't recursed too much */

    if (redirect_count > REDIRECT_MAX) {
        ui_error(rsrc->op, "redirection max count exceeded " 
                 "(looping redirect?)");
        redirect_count = 0;
        return FALSE;
    }
        
    /* make sure everything's initialized to something useful */
    u = rsrc->url;
     
    if (! *(u->host)) {
        ui_error(rsrc->op, "no host specified");
        return FALSE;
    }

    /* fill in proxyness */
    if (!rsrc->proxy) {
        rsrc->proxy = get_proxy("HTTP_PROXY");
    }
        
    if (!u->path) u->path = strdup("/");

    if (!u->file) u->file = strdup("");  /* funny looking */

    if (!u->port) u->port = 80;

    /* send the request to either the proxy or the remote host */
    if (rsrc->proxy) {
        proxy_url = url_new();
        url_init(proxy_url, rsrc->proxy);
                
        if (!proxy_url->port) proxy_url->port = 80;

        if (!proxy_url->host) {
            ui_error(rsrc->op, "bad proxy `%s'", rsrc->proxy);
            return FALSE;
        }

        if (proxy_url->username)
            rsrc->proxy_username = strdup(proxy_url->username);

        if (proxy_url->password)
            rsrc->proxy_password = strdup(proxy_url->password);

        /* Prompt for proxy password if not specified */
        if (proxy_url->username && !proxy_url->password) {
            proxy_url->password = ui_get_input(rsrc->op, NULL,
                                               "Password for "
                                               "proxy %s@%s",
                                               proxy_url->username,
                                               proxy_url->host);
        }

        if (!(sock = tcp_connect(rsrc->op, proxy_url->host, proxy_url->port)))
            return FALSE;
        
        u->path = strdup("");
        u->file = strdup(u->full_url);
        request = get_request(rsrc);

        write(sock, request, strlen(request));

    } else /* no proxy */ {

        if (!(sock = tcp_connect(rsrc->op, u->host, u->port))) return FALSE;

        request = get_request(rsrc);
        write(sock, request, strlen(request));
    }

    /* check to see if it returned an HTTP 1.x response */
    memset(buf, '\0', 5);

    bytes_read = read(sock, buf, 8);

    if (bytes_read == 0) {
        close(sock);
        return FALSE;
    }

    if (!(buf[0] == 'H' && buf[1] == 'T' 
          && buf[2] == 'T' && buf[3] == 'P')) {
        write(rsrc->out_fd, buf, bytes_read);
    } else {
        /* skip the header */
        buf[bytes_read] = '\0';
        raw_header = get_raw_header(sock);
        raw_header = nvstrcat(buf, raw_header, NULL);
        header = make_http_header(raw_header);
                
        /* if in expert mode, write the raw_header to the log */
                
        ui_expert(rsrc->op, raw_header);

        /* check for redirects */
        new_location = get_header_value("location", header);

        if (raw_header[9] == '3' && new_location) {
            redir_u = url_new();
                        
            /* make sure we still send user/password along */
            redir_u->username = nvstrdup(u->username);
            redir_u->password = nvstrdup(u->password);

            url_init(redir_u, new_location);
            rsrc->url = redir_u;
            redirect_count++;
            retval = transfer(rsrc);
            goto cleanup;
        }
        
        if (raw_header[9] == '4' || raw_header[9] == '5') {
            for(i=0; raw_header[i] && raw_header[i] != '\n'; i++);
            raw_header[i] = '\0';

            if (!(rsrc->flags & SNARF_FLAGS_DOWNLOAD_SILENT)) {
                ui_error(rsrc->op, "HTTP error from server: %s", raw_header);
            }
            
            retval = FALSE;
            goto cleanup;
        }
                        
        len_string = get_header_value("content-length", header);

        if (len_string)
            rsrc->outfile_size = (off_t )atoi(len_string);

        if (get_header_value("content-range", header))
            rsrc->outfile_size += rsrc->outfile_offset;
    }

    retval = dump_data(rsrc, sock);
        
 cleanup:
    free_http_header(header);
    close(sock);
    return retval;

} /* http_transfer() */
