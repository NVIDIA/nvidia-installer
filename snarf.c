/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 *
 *
 * snarf.c - this source file contains functions for downloading
 * files.  Based on snarf (http://www.xach.com/snarf/) by Zachary
 * Beane <xach@xach.com>, released under the GPL.
 */

#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>

#include "nvidia-installer.h"

#include "snarf.h"
#include "snarf-internal.h"

#include "user-interface.h"
#include "misc.h"


static const char *get_url_service_type(const char *string, Url *u);
static const char *get_url_username    (const char *string, Url *u);
static const char *get_url_password    (const char *string, Url *u);
static const char *get_url_hostname    (const char *url, Url *u);
static const char *get_url_port        (const char *url, Url *u);
static const char *get_url_path        (const char *url, Url *u);
static const char *get_url_file        (const char *string, Url *u);

static UrlResource *url_resource_new(void);
static void url_resource_destroy(UrlResource *rsrc);

static const char *local_hstrerror(int n);

/*
 * snarf() - main entry point 
 */

int snarf(Options *op, const char *url, int out_fd, uint32 flags)
{
    UrlResource *rsrc = NULL;
    int ret;
    
    if (op->no_network) {
        ui_error(op, "Unable to access file '%s', because the '--no-network' "
                 "commandline option was specified.", url);
        return FALSE;
    }

    rsrc = url_resource_new();
    rsrc->url = url_new();
    rsrc->op = op;
    
    if (url_init(rsrc->url, url) == NULL) {
        ui_error(op, "'%s' is not a valid URL.", url);
        return FALSE;
    }
    
    rsrc->out_fd = out_fd;
    rsrc->flags = flags;
    
    ret = transfer(rsrc);
    url_resource_destroy(rsrc);
    
    return ret;

} /* snarf() */



Url *url_new(void)
{
    Url *new_url;

    new_url = nvalloc(sizeof(Url));
    
    new_url->full_url     = NULL;
    new_url->service_type = 0;
    new_url->username     = NULL;
    new_url->password     = NULL;
    new_url->host         = NULL;
    new_url->port         = 0;
    new_url->path         = NULL;
    new_url->file         = NULL;
    
    return new_url;

} /* url_new() */



Url *url_init(Url *u, const char *string)
{
    const char *sp = string;

    u->full_url = nvstrdup(string);

    if (!(sp = get_url_service_type(sp, u))) return NULL;

    /* 
     * only get username/password if they are not null,
     * allows us to handle redirects properly
     */

    if (!u->username) 
        sp = get_url_username(sp, u);
    if (!u->password)
        sp = get_url_password(sp, u);

    sp = get_url_hostname(sp, u);

    if (!(u->host && *(u->host))) return NULL;

    sp = get_url_port(sp, u);

    sp = get_url_path(sp, u);
    sp = get_url_file(sp, u);

    return u;

} /* url_init() */



void url_destroy(Url *u)
{
    if (!u) return;
        
    nvfree(u->full_url);
    nvfree(u->username);
    nvfree(u->password);
    nvfree(u->host);
    nvfree(u->path);
    nvfree(u->file);

} /* url_destroy() */

        

char *get_proxy(const char *firstchoice)
{
    char *proxy;
    char *help;

    if ((proxy = getenv(firstchoice))) return proxy;
    
    help = nvstrdup(firstchoice);
    nvstrtolower(help);
    proxy = getenv(help);
    nvfree(help);
    if (proxy) return proxy;

    if ((proxy = getenv("SNARF_PROXY"))) return proxy;

    if ((proxy = getenv("PROXY"))) return proxy;

    return NULL;

} /* get_proxy() */



int tcp_connect(Options *op, char *remote_host, int port)
{
    struct hostent *host;
    struct sockaddr_in sa;
    int sock_fd;

    if ((host = (struct hostent *)gethostbyname(remote_host)) == NULL) {
        ui_error(op, "Unable to connect to %s (%s)",
                 remote_host, local_hstrerror(h_errno));
        return FALSE;
    }

    /* get the socket */
    if((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ui_error(op, "Unable to create socket (%s)", strerror(errno));
        return FALSE;
    }

    /* connect the socket, filling in the important stuff */
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, host->h_addr,host->h_length);
    
    if(connect(sock_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0){
        ui_error(op, "Unable to connect to remote host %s (%s)", 
                 remote_host, strerror(errno));
        return FALSE;
    }

    return sock_fd;

} /* tcp_connect() */



int dump_data(UrlResource *rsrc, int sock)
{
    int bytes_read = 0;
    int total_bytes_read = 0;
    ssize_t written = 0;
    char buf[SNARF_BUFSIZE];
    char *msg;
    float percent;
        
    msg = nvstrcat("Downloading: ", rsrc->url->full_url, NULL);
        
    if (rsrc->flags & SNARF_FLAGS_STATUS_BAR) {
        ui_status_begin(rsrc->op, msg, "Downloading");
    }
    
    while ((bytes_read = read(sock, buf, SNARF_BUFSIZE)) > 0) {

        if (rsrc->flags & SNARF_FLAGS_STATUS_BAR) {
            total_bytes_read += bytes_read;
            percent = (float) total_bytes_read / (float) rsrc->outfile_size;
            ui_status_update(rsrc->op, percent, NULL);
        }
        
        written = write(rsrc->out_fd, buf, bytes_read);
        if (written == -1) {
            ui_error(rsrc->op, "Error while writing output file (%s)",
                     strerror(errno));
            close(sock);
            return FALSE;
        }
    }

    close(sock);
        
    if (rsrc->flags & SNARF_FLAGS_STATUS_BAR) {
        ui_status_end(rsrc->op, "done.");
    }

    free(msg);

    return 1;

} /* dump_data() */



int transfer(UrlResource *rsrc)
{
    int ret = FALSE;

    switch (rsrc->url->service_type) {
    case SERVICE_HTTP:
        ret = http_transfer(rsrc);
        break;
    case SERVICE_FTP:
        ret = ftp_transfer(rsrc);
        break;
    default:
        ret = FALSE;
        break;
    }
    
    return ret;
}



/*************************************************************************/
/* static functions */

static const char *get_url_service_type(const char *string, Url *u)
{
    /*
     * fixme: what if the string isn't at the beginning of the string?
     */
    
    if (strstr(string, "http://")) {
        string += 7;
        u->service_type = SERVICE_HTTP;
        return string;
    }

    if (strstr(string, "ftp://")) {
        string += 6;
        u->service_type = SERVICE_FTP;
        return string;
    }

    if (strncasecmp(string, "www", 3) == 0) {
        u->service_type = SERVICE_HTTP;
        u->full_url = nvstrcat("http://", u->full_url, NULL);
        return string;
    }

    if (strncasecmp(string, "ftp", 3) == 0) {
        u->service_type = SERVICE_FTP;
        u->full_url = nvstrcat("ftp://", u->full_url, NULL);
        return string;
    }
    
    /* default to browser-style serviceless http URL */
    u->full_url = nvstrcat("http://", u->full_url, NULL);
    u->service_type = SERVICE_HTTP;
    return string;

} /* get_url_service_type() */



static const char *get_url_username(const char *string, Url *u)
{
    int i;
    char *username;
    char *at;
    char *slash;
        
    at = strchr(string, '@');
    slash = strchr(string, '/');

    if ((!at) || (slash && (at >= slash))) return string;
        
    for (i = 0; string[i] && string[i] != ':' && string[i] != '@' &&
             string[i] != '/'; i++);

    if (string[i] != '@' && string[i] != ':') return string;
    
    username = nvalloc(i);
    memcpy(username, string, i + 1);

    username[i] = '\0';

    string += i + 1;

    u->username = username;
    return string;

} /* get_url_username() */



static const char *get_url_password(const char *string, Url *u)
{
    int i;
    char *password;
    char *at;
    char *slash;
        
    at = strchr(string, '@');
    slash = strchr(string, '/');

    if ((!at) || (slash && (at >= slash))) return string;
    
    /*
     * skipping to the end of the host portion.  this is kinda messy
     * for the (rare) cases where someone has a slash and/or at in
     * their password. It's not perfect; but it catches easy cases.
     *    
     * If you know of a better way to do this, be my guest. I do not
     * feel a particular paternal instinct towards my ugly code.
     *
     * I guess that applies to this whole program.
     */

    for (i = 0 ; string[i] != '@'; i++);
        
    password = nvalloc(i);

    /* and finally, get the password portion */
    
    memcpy(password, string, i);
    password[i] = '\0';

    string += i + 1;

    u->password = password;
        
    return string;

} /* get_url_password() */



static const char *get_url_hostname(const char *url, Url *u)
{
    char *hostname;
    int i;

    /* skip to end, slash, or port colon */
    for (i = 0; url[i] && url[i] != '/' && url[i] != ':'; i++);

    hostname = nvalloc(i + 1);

    memcpy(hostname, url, i);

    hostname[i] = '\0';

    /* if there's a port */
    if(url[i] == ':')
        url += i + 1;
    else
        url += i;

    u->host = hostname;
    return url;

} /* get_url_hostname() */



static const char *get_url_port(const char *url, Url *u)
{
    char *port_string;
    int i;
    
    for (i = 0; isdigit(url[i]); i++);

    if (i == 0) return url;

    port_string = nvalloc(i + 1);
    memcpy(port_string, url, i + 1);

    port_string[i] = '\0';

    url += i;
    
    u->port = atoi(port_string);

    return url;

} /* get_url_port() */



static const char *get_url_path(const char *url, Url *u)
{
    int i;
    char *path;

    /* find where the last slash is */
    for (i = strlen(url); i > 0 && url[i] != '/'; i--);
    
    if (url[i] != '/') return url;

    path = nvalloc(i + 2);
    memcpy(path, url, i + 1);
    path[i] = '/';
    path[i + 1] = '\0';

    url += i + 1;
    u->path = path;
    
    return url;

} /* get_url_path() */



static const char *get_url_file(const char *string, Url *u)
{
    char *file;
        
    if (!string[0]) return NULL;

    file = nvalloc(strlen(string) + 1);

    memcpy(file, string, strlen(string) + 1);

    u->file = file;

    return string;

} /* get_url_file() */



static UrlResource *url_resource_new(void)
{
    UrlResource *new_resource;
        
    new_resource = nvalloc(sizeof(UrlResource));
    
    new_resource->url = NULL;
    new_resource->out_fd = 0;
    new_resource->proxy = NULL;
    new_resource->proxy_username = NULL;
    new_resource->proxy_password = NULL;
    new_resource->op = NULL;
    new_resource->outfile_size = 0;
    new_resource->outfile_offset = 0;
    
    return new_resource;

} /* url_resource_new() */



static void url_resource_destroy(UrlResource *rsrc)
{
    if (!rsrc) return;

    if(rsrc->url) url_destroy(rsrc->url);

    free(rsrc);

} /* url_resource_destroy() */



static const char *local_hstrerror(int n)
{
    switch (n) {
      case HOST_NOT_FOUND: return "unknown host";
      case NO_ADDRESS:     return "no IP address associated with host";
      case NO_RECOVERY:    return "fatal DNS error";
      case TRY_AGAIN:      return "temporary DNS error (try again later)";
      default:             return "unknown error";
    }
} /* local_hstrerror() */
