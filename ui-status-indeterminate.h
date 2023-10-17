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

#ifndef __UI_STATUS_INDETERMINATE_H__
#define __UI_STATUS_INDETERMINATE_H__

/*
 * Current state of the indeterminate status indicator, if any:
 *
 * INDETERMINATE_INVALID:  The indeterminate status indicator is broken. All of
 *                         the indeterminate functions should be no-ops.
 * INDETERMINATE_INACTIVE: The task which the indeterminate status indicator is
 *                         tracking has not been started or is complete.
 * INDETERMINATE_ACTIVE:   The task which the indeterminate status indicator is
 *                         tracking is actively running.
 */

typedef enum {
    INDETERMINATE_INVALID,
    INDETERMINATE_INACTIVE,
    INDETERMINATE_ACTIVE,
} IndeterminateState;

/*
 * Opaque object used by callers
 */

typedef struct __indeterminate_data IndeterminateData;

IndeterminateData *indeterminate_init(void);
void indeterminate_destroy(IndeterminateData *d);
IndeterminateState indeterminate_get(IndeterminateData *d);
void indeterminate_begin(IndeterminateData *d, void *(*w)(void *), void *args);
void indeterminate_end(IndeterminateData *d);
#endif
