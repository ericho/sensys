/*
 * Copyright (c) 2013-2016 Intel Corporation. All rights reserved.
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
 * CORETEMP resource manager sensor
 */
#ifndef ORCM_SENSOR_CORETEMP_H
#define ORCM_SENSOR_CORETEMP_H

#include "orcm_config.h"

#include "orcm/mca/sensor/sensor.h"

BEGIN_C_DECLS

typedef struct {
    orcm_sensor_base_component_t super;
    bool test;
    bool enable_packagetemp;
    char *policy;
    bool use_progress_thread;
    int sample_rate;
    bool collect_metrics;
    void* runtime_metrics;
    int64_t diagnostics;
} orcm_sensor_coretemp_component_t;

typedef struct {
    opal_event_base_t *ev_base;
    bool ev_active;
    int sample_rate;
} orcm_sensor_coretemp_t;

ORCM_MODULE_DECLSPEC extern orcm_sensor_coretemp_component_t mca_sensor_coretemp_component;
extern orcm_sensor_base_module_t orcm_sensor_coretemp_module;


END_C_DECLS

#endif
