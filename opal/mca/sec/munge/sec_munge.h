/*
 * Copyright (c) 2015      Intel Corporation. All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef OPAL_SEC_MUNGE_H
#define OPAL_SEC_MUNGE_H

#include "opal/mca/sec/sec.h"

BEGIN_C_DECLS


OPAL_MODULE_DECLSPEC extern opal_sec_base_component_t mca_sec_munge_component;
OPAL_DECLSPEC extern opal_sec_base_module_t opal_sec_munge_module;
extern char* authorize_grp;

END_C_DECLS

#endif /* OPAL_SEC_MUNGE_H */
