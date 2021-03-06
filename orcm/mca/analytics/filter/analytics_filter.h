/*
 * Copyright (c) 2014      Intel Corporation. All rights reserved.
 *
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

/**
 * @file
 * 
 */

#ifndef MCA_analytics_filter_EXPORT_H
#define MCA_analytics_filter_EXPORT_H

#include "orcm_config.h"

#include "orcm/mca/analytics/analytics.h"

BEGIN_C_DECLS

/*
 * Local Component structures
 */

ORCM_MODULE_DECLSPEC extern orcm_analytics_base_component_t mca_analytics_filter_component;

typedef struct {
    orcm_analytics_base_module_t api;
} mca_analytics_filter_module_t;
ORCM_DECLSPEC extern mca_analytics_filter_module_t orcm_analytics_filter_module;

typedef struct {
    uint64_t unique_id;
    bool unique_id_generated;
} filter_key_t;
END_C_DECLS

#endif /* MCA_analytics_threshold_EXPORT_H */
