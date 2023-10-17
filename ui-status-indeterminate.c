/*
 * Copyright (C) 2023 NVIDIA Corporation
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
 */

#include <stdlib.h>
#include <pthread.h>

#include "ui-status-indeterminate.h"

struct __indeterminate_data {
    pthread_mutex_t mutex;
    pthread_t thread;
    IndeterminateState state;
};

/* Allocate an IndeterminateData and initialize its mutex */

IndeterminateData *indeterminate_init(void)
{
    IndeterminateData *ret = calloc(1, sizeof(*ret));

    if (ret) {
        if (pthread_mutex_init(&ret->mutex, NULL) == 0) {
            ret->state = INDETERMINATE_INACTIVE;
        } else {
            ret->state = INDETERMINATE_INVALID;
        }
    }

    return ret;
}

/* Clean up IndeterminateData resources */

void indeterminate_destroy(IndeterminateData *d)
{
    if (d) {
        if (d->state != INDETERMINATE_INVALID) {
            pthread_mutex_destroy(&d->mutex);
        }
        free(d);
    }
}

/* Get the current state of the indeterminate progress bar */

IndeterminateState indeterminate_get(IndeterminateData *d)
{
    IndeterminateState state = INDETERMINATE_INVALID;

    if (d && d->state != INDETERMINATE_INVALID) {
        if (!pthread_mutex_lock(&d->mutex)) {
            state = d->state;
            if (pthread_mutex_unlock(&d->mutex)) {
                state = INDETERMINATE_INVALID;
            }
        }
    }

    return state;
}

static void indeterminate_set(IndeterminateData *d, IndeterminateState state)
{
    if (!d || d->state == INDETERMINATE_INVALID) {
        return;
    } else if (pthread_mutex_lock(&d->mutex)) {
        d->state = INDETERMINATE_INVALID;
        return;
    }

    d->state = state;

    if (pthread_mutex_unlock(&d->mutex)) {
        d->state = INDETERMINATE_INVALID;
    }
}

/* Begin displaying a new indeterminate indicator, replacing any existing bar */

void indeterminate_begin(IndeterminateData *d, void *(*worker)(void *),
                         void *args)
{
    if (!d || indeterminate_get(d) == INDETERMINATE_INVALID) {
        return;
    }

    /* Finish any existing indeterminate progress indicator */
    indeterminate_end(d);

    if (pthread_create(&d->thread, NULL, worker, args)) {
        indeterminate_set(d, INDETERMINATE_INVALID);
    } else {
        indeterminate_set(d, INDETERMINATE_ACTIVE);
    }
}

/* Stop the indeterminate progress bar and wait for it to finish updating */

void indeterminate_end(IndeterminateData *d)
{
    void *join_ret;

    if (!d || indeterminate_get(d) != INDETERMINATE_ACTIVE) {
        return;
    }

    indeterminate_set(d, INDETERMINATE_INACTIVE);

    if (pthread_join(d->thread, &join_ret)) {
        indeterminate_set(d, INDETERMINATE_INVALID);
    }
}
