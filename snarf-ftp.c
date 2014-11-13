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
 * snarf-ftp.c - this source file contains functions for downloading
 * files via the ftp protocol.  Based on snarf
 * (http://www.xach.com/snarf/) by Zachary Beane <xach@xach.com>,
 * released under the GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdarg.h>

#include <netdb.h>

#ifdef USE_SOCKS5
#define SOCKS
#include <socks.h>
#endif

#include <errno.h>

#include "snarf-internal.h"
#include "user-interface.h"
#include "misc.h"



static void close_quit(int sock)
{
    if (sock) {
        write(sock, "QUIT\r\n", 6);
        close(sock);
    }
} /* close_quit() */



static void ftp_set_defaults(UrlResource *rsrc, Url *u)
{
    if (!u->port)     u->port = 21;
    if (!u->username) u->username = strdup("anonymous");
    if (!u->password) u->password = strdup("snarf@");

} /* ftp_set_defaults() */



static void send_control(int sock, char *string, ...)
{
    va_list args;
    char *line = NULL;
    char *newline;
    char *s = NULL;

    line = nvstrdup(string);
        
    va_start(args, string);
    s = va_arg(args, char *);
    while (s) {
        newline = nvstrcat(line, s, NULL);
        nvfree(line);
        line = newline;
        s = va_arg(args, char *);
    }
    va_end(args);

    write(sock, line, strlen(line));
    nvfree(line);

} /* send_control() */



static char *get_line(UrlResource *rsrc, int control)
{
    int bytes_read = 0;
    char *end;
    char buf[SNARF_BUFSIZE+1];

    while ((bytes_read = read(control, buf, SNARF_BUFSIZE)) > 0) {
                
        if (buf[0] == '4' || buf[0] == '5') return NULL;
        
        /* in case there's a partial read */
        buf[bytes_read] = '\0';
                
        if (buf[bytes_read - 1] == '\n') buf[bytes_read - 1] = '\0';

        if (buf[bytes_read - 2] == '\r') buf[bytes_read - 2] = '\0';

        ui_expert(rsrc->op, "%s", buf);
                
        if (isdigit(buf[0]) && buf[3] == ' ') {
            return strdup(buf);
        }

        /* skip to last line of possibly multiple line input */

        if ((end = strrchr(buf, '\n'))) {
            end++;
            if (isdigit(end[0]) && end[3] == ' ')
                return strdup(end);
        }
    }

    return NULL;

} /* get_line() */



static int check_numeric(const char *numeric, const char *buf)
{
    return ((buf[0] == numeric[0]) &&
            (buf[1] == numeric[1]) &&
            (buf[2] == numeric[2]));

} /* check_numeric() */



static int sock_init(Options *op, struct sockaddr_in *sa, int control)
{
    socklen_t i;
    int sock;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ui_error(op, "unable to open socket (%s)", strerror(errno));
        return(0);
    }

    i = sizeof(*sa); 

    getsockname(control, (struct sockaddr *)sa, &i); 
    sa->sin_port = 0 ; /* let system choose a port */
    if (bind (sock, (struct sockaddr *)sa, sizeof (*sa)) < 0) {
        ui_error(op, "unable to bind socket (%s)", strerror(errno));
        return 0;
    }

    return sock;

} /* sock_init() */



static int get_passive_sock(UrlResource *rsrc, int control)
{
    unsigned char *addr;
    struct sockaddr_in sa;
    int sock;
    int x;
    char *line, *orig_line;

    send_control(control, "PASV\r\n", NULL);

    if( !((line = get_line(rsrc, control)) &&
          check_numeric("227", line)) ) {
        nvfree(line);
        return 0;
    }

    orig_line = line;

    if (strlen(line) < 4) {
        nvfree(line);
        return 0;
    }

    if (!(sock = sock_init(rsrc->op, &sa, control))) return -1;

    /* skip the numeric response */
    line += 4;

    /* then find the digits */
        
    while (!(isdigit(*line))) line++;
    
    /* ugliness from snarf 1.x */

    sa.sin_family = AF_INET;
    addr = (unsigned char *)&sa.sin_addr;

    for(x = 0; x < 4; x++) {
        addr[x] = atoi(line);
        line = strchr(line,',') + 1;
    }

    addr = (unsigned char *)&sa.sin_port ;
    addr[0] = atoi(line);
    line = strchr(line,',') + 1;
    addr[1] = atoi(line);
        
    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        nvfree(orig_line);
        ui_error(rsrc->op, "unable to connect (%s)", strerror(errno));
        return -1;
    }

    nvfree(orig_line);
    return sock;

} /* get_passive_sock() */



static int get_sock(UrlResource *rsrc, int control)
{
    struct sockaddr_in sa;
    unsigned char *addr;
    unsigned char *port;
    char *line;
    char port_string[SNARF_BUFSIZE];
    unsigned int sock;
    socklen_t i;

    if (!(sock = sock_init(rsrc->op, &sa, control))) return 0;

        
    if (listen(sock, 0) < 0) {
        ui_error(rsrc->op, "unable to listen (%s)", strerror(errno));
        return 0;
    }
        
    i = sizeof(sa);

    getsockname(sock, (struct sockaddr *)&sa, &i);

    addr = (unsigned char *)(&sa.sin_addr.s_addr);
    port = (unsigned char *)(&sa.sin_port);

    sprintf(port_string, "PORT %d,%d,%d,%d,%d,%d\r\n", 
            addr[0], addr[1], addr[2], addr[3],
            port[0],(unsigned char)port[1]);

    send_control(control, port_string, NULL);

    if (!((line = get_line(rsrc, control)) && check_numeric("200", line))) {
        nvfree(line);
        return 0;
    }
    nvfree(line);
        
    return sock;

} /* get_sock() */



/* I'm going to go to hell for not doing proper cleanup. */
        
int ftp_transfer(UrlResource *rsrc)
{
    Url  *u         = NULL;
    char *line      = NULL;
    int   sock      = 0;
    int   data_sock = 0;
    int   passive   = 1;
    int   retval    = 0;
    
    u = rsrc->url;

    /*
     * first of all, if this is proxied, just pass it off to the
     * http module, since that's how we support proxying.
     */

    rsrc->proxy = get_proxy("FTP_PROXY");

    if (rsrc->proxy && (rsrc->proxy[0] != '\0')) {
        return http_transfer(rsrc);
    }

    ftp_set_defaults(rsrc, u);

    if (!(sock = tcp_connect(rsrc->op, u->host, u->port)))
        return FALSE;

    if (!(line = get_line(rsrc, sock)))
        return FALSE;

    if (!check_numeric("220", line)) {
        ui_error(rsrc->op, "bad ftp server greeting: %s", line);
        nvfree(line);
        return FALSE;
    }
    
    send_control(sock, "USER ", u->username, "\r\n", NULL);

    if (!(line = get_line(rsrc, sock))) return FALSE;
    
    /* do the password dance */
    if (!check_numeric("230", line)) {
        if (!check_numeric("331", line)) {
            ui_error(rsrc->op, "bad/unexpected response: %s", line);
            nvfree(line);
            return FALSE;
        } else {
            nvfree(line);

            send_control(sock, "PASS ", u->password, "\r\n", NULL);
                        
            if (!((line = get_line(rsrc, sock)) &&
                  check_numeric("230", line)) ) {
                nvfree(line);
                ui_error(rsrc->op, "login failed");
                return FALSE;
            }
            nvfree(line);
        }
    }
        
    /* set binmode */
    send_control(sock, "TYPE I\r\n", NULL);

    if (!(line = get_line(rsrc, sock))) return 0;
    nvfree(line);

    if (u->path) {
        send_control(sock, "CWD ", u->path, "\r\n", NULL);
        
        if (!((line = get_line(rsrc, sock)) &&
              check_numeric("250", line))) {
            nvfree(line);
            close_quit(sock);
            return 0;
        }
        nvfree(line);
    }
        
    /* finally, the good stuff */
    
    /* get a socket for reading. try passive first. */
        
    if ((data_sock = get_passive_sock(rsrc, sock)) == -1) {
        return FALSE;
    }
    
    if (!data_sock) {
        if ((data_sock = get_sock(rsrc, sock)) < 1)
            return 0;
        else
            passive = 0;
    }

    if (u->file) {
        send_control(sock, "SIZE ", u->file, "\r\n", NULL);
        line = get_line(rsrc, sock);
        if (line && check_numeric("213", line)) {
            rsrc->outfile_size = atoi(line + 3);
        } else {
            rsrc->outfile_size = 0;
        }
    }
    
    if (u->file)
        send_control(sock, "RETR ", u->file, "\r\n", NULL);
    else
        send_control(sock, "NLST\r\n", NULL);

    if (!((line = get_line(rsrc, sock)) &&
          (check_numeric("150", line) || check_numeric("125", line)))) {
        nvfree(line);
        close_quit(sock);
        return 0;
    }

    if (!passive) 
        data_sock = accept(data_sock, NULL, NULL);

    nvfree(line);

    retval = dump_data(rsrc, data_sock);
    
    line = get_line(rsrc, sock); /* 226 Transfer complete */
    nvfree(line);
    send_control(sock, "QUIT\r\n", NULL);
    line = get_line(rsrc, sock); /* 221 Goodbye */
    nvfree(line);
    
    close(sock);
    close(data_sock);
    return retval;

} /* ftp_transfer() */
