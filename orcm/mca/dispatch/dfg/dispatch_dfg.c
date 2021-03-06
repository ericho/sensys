/*
 * Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orcm_config.h"
#include "orcm/constants.h"

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif

#include "opal/class/opal_list.h"
#include "opal/dss/dss.h"
#include "opal/util/argv.h"
#include "opal/util/if.h"
#include "opal/util/net.h"
#include "opal/util/opal_environ.h"
#include "opal/util/show_help.h"

#include "orte/util/name_fns.h"
#include "orte/runtime/orte_globals.h"
#include "orte/runtime/orte_wait.h"
#include "orte/util/show_help.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/notifier/base/base.h"
#include "orte/mca/notifier/notifier.h"

#include "orcm/mca/db/db.h"
#include "orcm/runtime/orcm_globals.h"
#include "orcm/util/utils.h"
#include "orcm/mca/dispatch/base/base.h"
#include "orcm/mca/dispatch/dfg/dispatch_dfg.h"
#include "orcm/mca/analytics/base/analytics_private.h"
#include "orcm/mca/scd/base/base.h"
#include "orte/mca/rml/rml.h"
#include "orcm/tools/octl/common.h"

/* API functions */

static void dfg_init(void);
static void dfg_finalize(void);
static void dfg_generate(orcm_ras_event_t *cd);

/* The module struct */

orcm_dispatch_base_module_t orcm_dispatch_dfg_module = {
    dfg_init,
    dfg_finalize,
    dfg_generate
};

/* local variables */
static int env_db_commit_count[MAX_NUM_THREAD] = {0};

static void dfg_env_db_open_cbfunc(int dbh, int status, opal_list_t *input_list,
                                   opal_list_t *output_list, void *cbdata)
{
    if (0 != status) {
        OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                             "%s dispatch:dfg DB env open operation failed",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
    }
    SAFE_RELEASE(input_list);
}

static void dfg_event_db_open_cbfunc(int dbh, int status, opal_list_t *input_list,
                                     opal_list_t *output_list, void *cbdata)
{
    if (0 != status) {
        OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                             "%s dispatch:dfg DB event open operation failed",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
    }
    SAFE_RELEASE(input_list);
}

static void dfg_data_cbfunc(int dbh, int status, opal_list_t *input_list,
                            opal_list_t *output_list, void *cbdata)
{
    SAFE_RELEASE(input_list);
}

static opal_list_t* dfg_init_env_dbhandle_commit_rate(void)
{
    opal_list_t *props = OBJ_NEW(opal_list_t); /* DB Attributes list */
    orcm_value_t *attribute = OBJ_NEW(orcm_value_t);

    attribute->value.key = strdup("autocommit");
    attribute->value.type = OPAL_BOOL;
    if (1 < orcm_dispatch_base.sensor_db_commit_rate) {
        attribute->value.data.flag = false; /* Disable Auto commit/Enable grouped commits */
    } else {
        attribute->value.data.flag = true; /* Enable Auto commit/Disable grouped commits */
    }
    opal_list_append(props, (opal_list_item_t *)attribute);

    return props;
}

static void dfg_init(void)
{
    opal_list_t *props = NULL;

    OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                         "%s dispatch:dfg init",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    /* get a db handle for env data*/
    props = dfg_init_env_dbhandle_commit_rate();
    orcm_db.open_multi_thread_select(ORCM_DB_ENV_DATA, props, dfg_env_db_open_cbfunc, NULL);
    orcm_db.open_multi_thread_select(ORCM_DB_EVENT_DATA, NULL, dfg_event_db_open_cbfunc, NULL);
}

static void dfg_finalize(void)
{
    OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                         "%s dispatch:dfg finalize",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
    orcm_db.close_multi_thread_select(ORCM_DB_ENV_DATA, NULL, NULL);
    orcm_db.close_multi_thread_select(ORCM_DB_EVENT_DATA, NULL, NULL);
}

static opal_list_t* dfg_convert_event_data_to_list(orcm_ras_event_t *ecd)
{
    orcm_value_t *metric = NULL;
    struct timeval eventtime;
    opal_list_t *input_list = NULL;

    input_list = OBJ_NEW(opal_list_t);
    if (NULL == input_list) {
        ORTE_ERROR_LOG(ORCM_ERR_OUT_OF_RESOURCE);
        return NULL;
    }

    metric = orcm_util_load_orcm_value("type", (void *) orcm_dispatch_base_print_type(ecd->type), OPAL_STRING, NULL);
    if (NULL == metric) {
        ORTE_ERROR_LOG(ORCM_ERR_OUT_OF_RESOURCE);
        OBJ_RELEASE(input_list);
        return NULL;
    }
    opal_list_append(input_list, (opal_list_item_t *)metric);

    metric = orcm_util_load_orcm_value("severity", (void *) orcm_dispatch_base_print_severity(ecd->severity), OPAL_STRING, NULL);
    if (NULL == metric) {
        ORTE_ERROR_LOG(ORCM_ERR_OUT_OF_RESOURCE);
        OBJ_RELEASE(input_list);
        return NULL;
    }
    opal_list_append(input_list, (opal_list_item_t *)metric);

    eventtime.tv_sec = ecd->timestamp;
    eventtime.tv_usec = 0L;

    metric = orcm_util_load_orcm_value("ctime", &eventtime, OPAL_TIMEVAL, NULL);
    if (NULL == metric) {
        ORTE_ERROR_LOG(ORCM_ERR_OUT_OF_RESOURCE);
        OBJ_RELEASE(input_list);
        return NULL;
    }
    opal_list_append(input_list, (opal_list_item_t *)metric);

    /*This is an O(1) operation. But contents of list are permanently transferred  */
    opal_list_join(input_list, (opal_list_item_t *)metric, &ecd->reporter);

    opal_list_join(input_list, (opal_list_item_t *)metric, &ecd->description);

    opal_list_join(input_list, (opal_list_item_t *)metric, &ecd->data);

    return input_list;
}

static void dfg_env_data_commit_cb(int dbhandle, int status, opal_list_t *in,
                                    opal_list_t *out, void *cbdata) {
    if (ORCM_SUCCESS != status) {
        ORTE_ERROR_LOG(status);
        orcm_db.rollback_multi_thread_select(dbhandle, NULL, NULL);
    }
}

static void dfg_generate_database_event(opal_list_t *input_list, int data_type)
{
    int thread_id = -1;
    if (ORCM_RAS_EVENT_SENSOR == data_type) {
        if (0 > (thread_id = orcm_db.store_multi_thread_select(ORCM_DB_ENV_DATA, input_list,
                                                               NULL, dfg_data_cbfunc, NULL))) {
            OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                                 "%s dispatch:dfg couldn't store env data as db handler isn't opened",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        } else if (orcm_dispatch_base.sensor_db_commit_rate > 1) {
            env_db_commit_count[thread_id]++;
            if (orcm_dispatch_base.sensor_db_commit_rate == env_db_commit_count[thread_id]) {
                orcm_db.commit_multi_thread_select(ORCM_DB_ENV_DATA,
                                                   thread_id, dfg_env_data_commit_cb, NULL);
                env_db_commit_count[thread_id] = 0;
            }
        }
    } else if (0 > (thread_id = orcm_db.store_multi_thread_select(ORCM_DB_EVENT_DATA, input_list,
                                                                  NULL, dfg_data_cbfunc, NULL))) {
        OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                            "%s dispatch:dfg couldn't store event data as db handler isn't opened",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
    }
}

static opal_buffer_t* dfg_pack_exec_early_ret(opal_buffer_t *buf, int rc)
{
    SAFE_RELEASE(buf);
    ORTE_ERROR_LOG(rc);
    return NULL;
}

static opal_buffer_t* dfg_pack_exec(char *exec_name, char *exec_argv)
{
    int rc = ORCM_SUCCESS;
    opal_buffer_t *buf = NULL;
    orcm_scd_cmd_flag_t command = ORCM_DISPATCH_LAUNCH_EXEC_COMMAND;
    bool exec_argv_set = (NULL != exec_argv ? true : false);

    if (NULL == (buf = OBJ_NEW(opal_buffer_t))) {
        return dfg_pack_exec_early_ret(buf, ORCM_ERR_OUT_OF_RESOURCE);
    }

    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &command, 1, ORCM_SCD_CMD_T))) {
        return dfg_pack_exec_early_ret(buf, rc);
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &exec_name, 1, OPAL_STRING))) {
        return dfg_pack_exec_early_ret(buf, rc);
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &exec_argv_set, 1, OPAL_BOOL))) {
        return dfg_pack_exec_early_ret(buf, rc);
    }
    if (exec_argv_set && OPAL_SUCCESS != (rc = opal_dss.pack(buf, &exec_argv, 1, OPAL_STRING))) {
        return dfg_pack_exec_early_ret(buf, rc);
    }

    return buf;
}

static int dfg_send_buffer(orte_rml_recv_cb_t* xfer, opal_buffer_t* buf)
{
    int rc = ORCM_SUCCESS;

    xfer->active = true;
    orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD,
                            ORTE_RML_NON_PERSISTENT, orte_rml_recv_callback, xfer);
    if (ORTE_SUCCESS == (rc = orte_rml.send_buffer_nb(ORTE_PROC_MY_SCHEDULER, buf,
                         ORCM_RML_TAG_SCD, orte_rml_send_callback, NULL))) {
        ORCM_WAIT_FOR_COMPLETION(xfer->active, ORCM_OCTL_WAIT_TIMEOUT, &rc);
    } else {
        SAFE_RELEASE(buf);
    }
    SAFE_RELEASE(xfer);

    orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);

    return rc;
}

static void dfg_send_exec(char *exec_name, char *exec_argv)
{
    int rc = ORCM_SUCCESS;
    opal_buffer_t *buf = NULL;
    orte_rml_recv_cb_t *xfer = NULL;

    if (NULL == exec_name || NULL == (buf = dfg_pack_exec(exec_name, exec_argv))) {
        return;
    }

    if (NULL == (xfer = OBJ_NEW(orte_rml_recv_cb_t))) {
        SAFE_RELEASE(buf);
        ORTE_ERROR_LOG(ORCM_ERR_OUT_OF_RESOURCE);
        return;
    }

    if (ORTE_SUCCESS != (rc = dfg_send_buffer(xfer, buf))) {
        ORTE_ERROR_LOG(rc);
    }
}

static int dfg_iterate_ras_desc(orcm_ras_event_t *ecd, char **notifier_msg,
                                char **notifier_action, char **exec_name, char** exec_argv)
{
    orcm_value_t *list_item = NULL;

    OPAL_LIST_FOREACH(list_item, &ecd->description, orcm_value_t) {
        if (NULL == list_item || NULL == list_item->value.key) {
            return ORCM_ERR_BAD_PARAM;
        }
        if (0 == strcmp(list_item->value.key, "notifier_msg") &&
            NULL != list_item->value.data.string && NULL == *notifier_msg) {
            *notifier_msg = strdup(list_item->value.data.string);
        } else if (0 == strcmp(list_item->value.key, "notifier_action") &&
                   NULL != list_item->value.data.string && NULL == *notifier_action) {
            *notifier_action = strdup(list_item->value.data.string);
        } else if (0 == strcmp(list_item->value.key, "exec_name") &&
                   NULL != list_item->value.data.string && NULL == *exec_name) {
            *exec_name = strdup(list_item->value.data.string);
        } else if (0 == strcmp(list_item->value.key, "exec_argv") &&
                   NULL != list_item->value.data.string && NULL == *exec_argv) {
            *exec_argv = strdup(list_item->value.data.string);
        }
    }

    return ((NULL == *notifier_msg || NULL == *notifier_action) ? ORCM_ERROR : ORCM_SUCCESS);
}

static void dfg_free_mem(char *notifier_msg, char *notifier_action,
                         char *exec_name, char *exec_argv)
{
    SAFEFREE(notifier_msg);
    SAFEFREE(notifier_action);
    SAFEFREE(exec_name);
    SAFEFREE(exec_argv);
}

static void dfg_generate_notifier_event(orcm_ras_event_t *ecd)
{
    int rc = ORCM_SUCCESS;
    char *notifier_msg = NULL;
    char *notifier_action = NULL;
    char *exec_name = NULL;
    char *exec_argv = NULL;

    rc = dfg_iterate_ras_desc(ecd, &notifier_msg, &notifier_action, &exec_name, &exec_argv);
    if (ORCM_SUCCESS != rc) {
        ORTE_ERROR_LOG(rc);
        dfg_free_mem(notifier_msg, notifier_action, exec_name, exec_argv);
        return;
    }

    if (ORCM_RAS_SEVERITY_EMERG <= ecd->severity && ORCM_RAS_SEVERITY_UNKNOWN > ecd->severity) {
        OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                             "%s dispatch:dfg generating notifier with severity %d "
                             "and notifier action as %s", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ecd->severity, notifier_action));
        if (0 != strncmp(notifier_action, "exec", strlen(notifier_action))) {
            ORTE_NOTIFIER_SYSTEM_EVENT(ecd->severity, notifier_msg, notifier_action);
        } else {
            dfg_free_mem(notifier_msg, notifier_action, NULL, NULL);
            dfg_send_exec(exec_name, exec_argv);
        }
    } else {
        dfg_free_mem(notifier_msg, notifier_action, NULL, NULL);
    }

    dfg_free_mem(NULL, NULL, exec_name, exec_argv);
}

static void dfg_convert_and_log_data_to_db(orcm_ras_event_t* ecd)
{
    opal_list_t *input_list = NULL;

    input_list = dfg_convert_event_data_to_list(ecd);

    if (NULL == input_list) {
        return;
    }

    dfg_generate_database_event(input_list, ecd->type);
}

static void dfg_generate_storage_events(orcm_ras_event_t *ecd)
{
    orcm_value_t *list_item = NULL;
    bool raw_db = false;
    bool is_valid_event_type = false;

    OPAL_LIST_FOREACH(list_item, &ecd->description, orcm_value_t) {
        if (NULL == list_item->value.key) {
            break;
        }
        if (0 == strcmp(list_item->value.key, "storage_type")) {
            switch (list_item->value.data.uint) {
            case ORCM_STORAGE_TYPE_NOTIFICATION:
                dfg_generate_notifier_event(ecd);
                break;

            //Send raw data to DB
            case ORCM_STORAGE_TYPE_DATABASE:
                raw_db = true;
                break;

            case ORCM_STORAGE_TYPE_PUBSUB:
                break;

            default:
                break;
            }

        }

    }
    if(true == raw_db)
        dfg_convert_and_log_data_to_db(ecd);

    //Store the event data into event table based on the MCA param.
    is_valid_event_type = (ORCM_RAS_EVENT_EXCEPTION == ecd->type ||
                                ORCM_RAS_EVENT_CHASSIS_ID_LED == ecd->type);
    if (is_valid_event_type && true == orcm_analytics_base.store_event_data) {
        dfg_convert_and_log_data_to_db(ecd);
    }

    return;
}

static void dfg_generate(orcm_ras_event_t *ecd)
{

    OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                         "%s dispatch:dfg record event",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    if (NULL == ecd) {
        OPAL_OUTPUT_VERBOSE((1, orcm_dispatch_base_framework.framework_output,
                             "%s dispatch:dfg NULL event data",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        return;
    }

    dfg_generate_storage_events(ecd);

    return;
}
