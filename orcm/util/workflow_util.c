/*
 * Copyright (c) 2016      Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orte/util/show_help.h"
#include "orte/runtime/orte_globals.h"

#include "orcm/runtime/orcm_globals.h"
#include "orcm/util/utils.h"
#include "orcm/mca/parser/parser.h"
#include "orcm/mca/parser/base/base.h"

#define WORKFLOW_UTIL_DEBUG_OUTPUT 10

opal_list_t* orcm_util_workflow_add_retrieve_workflows_section(const char *file)
{
    opal_list_t *result_list = NULL;
    int file_id;

    if (0 > (file_id = orcm_parser.open(file))) {
        ORCM_UTIL_MSG("%s workflow:util:Can't open workflow file",
                      ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
        return result_list;
    }

    result_list = orcm_parser.retrieve_section(file_id, "workflows", "");
    if (NULL == result_list) {
        ORCM_UTIL_MSG("%s workflow:util:Can't read workflow file",
                      ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
        orcm_parser.close(file_id);
        return result_list;
    }

    orcm_parser.close(file_id);

    return result_list;
}

static int orcm_util_workflow_add_check_filter_step(char *key, char *value, bool *is_filter_first_step)
{
    if ( (false == *is_filter_first_step) && (0 == strcmp("step", key)) ) {
        if (0 == strcmp("filter", value)) {
            *is_filter_first_step = true;
        }
        else {
            ORCM_UTIL_MSG("%s workflow:util:Filter plugin not present",
                          ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
            return ORCM_ERROR;
        }
    }
    return ORCM_SUCCESS;
}

static int orcm_util_workflow_add_extract_string_type (opal_buffer_t *buf, char *key, char *value, char *list_head_key, bool *is_name_first_key, bool *is_filter_first_step)
{
    int rc;

    if (false == *is_name_first_key) {
        if (0 == strcmp("name", key)) {
            *is_name_first_key = true;
        }
        else {
            ORCM_UTIL_MSG("%s workflow:util:'name' key is missing in 'workflow or 'step' groups",
                          ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
             return ORCM_ERROR;
        }
    }

    if (0 != strcmp("name", key)) {
        if (ORCM_SUCCESS != (rc = opal_dss.pack(buf, &key, 1, OPAL_STRING))) {
            return ORCM_ERROR;
        }
    }

    ORCM_UTIL_MSG("%s workflow:util:frame value is %s",
                  ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), value);

    rc = orcm_util_workflow_add_check_filter_step(list_head_key, value, is_filter_first_step);
    if (ORCM_SUCCESS != rc) {
        return rc;
    }

    if (ORCM_SUCCESS != (rc = opal_dss.pack(buf, &value, 1, OPAL_STRING))) {
        return ORCM_ERROR;
    }
    return ORCM_SUCCESS;
}

int orcm_util_workflow_add_extract_workflow_info(opal_list_t *result_list, opal_buffer_t *buf, char *list_head_key, bool *is_filter_first_step)
{
    orcm_value_t *list_item = NULL;
    int rc;
    int list_length = 0;
    bool is_name_first_key = false;

    if (NULL == result_list){
        return ORCM_ERROR;
    }

    list_length = (int)result_list->opal_list_length - 1;
    ORCM_UTIL_MSG("%s workflow:util:length of list is %d",
                  ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), list_length);

    if (ORCM_SUCCESS != (rc = opal_dss.pack(buf, &list_length, 1, OPAL_INT))) {
        return ORCM_ERROR;
    }

    rc = ORCM_ERROR;
    OPAL_LIST_FOREACH(list_item, result_list, orcm_value_t) {
        ORCM_UTIL_MSG("%s workflow:util:frame key is %s",
                      ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), list_item->value.key);

        if (list_item->value.type == OPAL_STRING) {
            rc = orcm_util_workflow_add_extract_string_type(buf, list_item->value.key, list_item->value.data.string, list_head_key, &is_name_first_key, is_filter_first_step);
            if (ORCM_SUCCESS != rc) {
                return rc;
            }

        } else if (list_item->value.type == OPAL_PTR) {
            if (false == is_name_first_key) {
                ORCM_UTIL_MSG("%s workflow:util:'name' key is missing in 'workflow or 'step' groups",
                              ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
                return ORCM_ERROR;
            }
            if (0 == strcmp(list_item->value.key, "step")) {
                rc = orcm_util_workflow_add_extract_workflow_info((opal_list_t*)list_item->value.data.ptr, buf, list_item->value.key, is_filter_first_step);
                if (ORCM_SUCCESS != rc) {
                    return rc;
                }
            }
            else {
                ORCM_UTIL_MSG("%s workflow:util: Unexpected Key in workflow file",
                              ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
                return ORCM_ERROR;
            }
        } else {
            ORCM_UTIL_MSG("%s workflow:util: Unexpected data type from parser framework",
                           ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
            return ORCM_ERROR;
        }
    }
    return rc;
}