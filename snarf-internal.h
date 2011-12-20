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
 * snarf-internal.h
 */

#ifndef __NVIDIA_INSTALLER_SNARF_INTERNAL_H__
#define __NVIDIA_INSTALLER_SNARF_INTERNAL_H__

#include "nvidia-installer.h"

typedef struct _UrlResource 	UrlResource;
typedef struct _Url		Url;

struct _Url {
    char *full_url;
    int service_type;
    char *username;
    char *password;
    char *host;
    int port;
    char *path;
    char *file;
};

struct _UrlResource {
    Url *url;
    Options *op;
    int out_fd;
    uint32 flags;
    char *proxy;
    char *proxy_username;
    char *proxy_password;
    off_t outfile_size;
    off_t outfile_offset;
};


/* Service types */
enum url_services {
    SERVICE_HTTP = 1,
    SERVICE_FTP
};


#define SNARF_BUFSIZE (5*2048)

/* snarf.c */

Url  *url_new    (void);
Url  *url_init   (Url *, const char *);
void  url_destroy(Url *);
char *get_proxy  (const char *);
int   dump_data  (UrlResource *, int);
int   tcp_connect(Options *, char *, int);
int transfer(UrlResource *rsrc);

/* snarf-ftp.c */

int ftp_transfer(UrlResource *);

/* snarf-http.c */

int http_transfer(UrlResource *);

#endif /* __NVIDIA_INSTALLER_SNARF_INTERNAL_H__ */
