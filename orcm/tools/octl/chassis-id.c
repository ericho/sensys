/*
 * Copyright (c) 2016  Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orte/mca/notifier/notifier.h"
#include "orcm/util/logical_group.h"
#include "orcm/tools/octl/common.h"

#include "orcm/util/utils.h"
#include "orcm/mca/parser/parser.h"
#include "orcm/mca/parser/base/base.h"
#include "orcm/mca/sensor/ipmi/ipmi_parser_interface.h"

#define MIN_CHASSIS_ID_ARG 3
#define MAX_CHASSIS_ID_ARG 4

#define LED_OFF 0
#define LED_TEMPORARY_ON 1
#define LED_INDEFINITE_ON 2

#define SAFE_ARGV_FREE(p) if(NULL != p) { opal_argv_free(p); p = NULL; }

static void cleanup(opal_buffer_t *buf, orte_rml_recv_cb_t *xfer);
static int begin_transaction(char* node, opal_buffer_t *buf, orte_rml_recv_cb_t *xfer);
static int unpack_responses_count(orte_rml_recv_cb_t *xfer);
static int unpack_response(char* node, orte_rml_recv_cb_t *xfer);
static int unpack_state(char* node, orte_rml_recv_cb_t *xfer);
int pack_chassis_id_data(opal_buffer_t *buf, orcm_cmd_server_flag_t *command,
        orcm_cmd_server_flag_t *sub_command, char **noderaw, unsigned char *seconds);
int orcm_octl_led_operation(orcm_cmd_server_flag_t command,
        orcm_cmd_server_flag_t sub_command, char *noderaw, unsigned char seconds);
int orcm_octl_chassis_id_state(char **argv);
int orcm_octl_chassis_id_on(char **argv);
int orcm_octl_chassis_id_off(char **argv);
static int open_parser_framework(void);
static void close_parser_framework(void);

static void cleanup(opal_buffer_t *buf, orte_rml_recv_cb_t *xfer){
    SAFE_RELEASE(buf);
    SAFE_RELEASE(xfer);
    orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_CMD_SERVER);
}


static int begin_transaction(char* node, opal_buffer_t *buf, orte_rml_recv_cb_t *xfer){
    int rc = ORCM_SUCCESS;
    xfer->active = true;
    orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD,
                            ORCM_RML_TAG_CMD_SERVER,
                            ORTE_RML_NON_PERSISTENT,
                            orte_rml_recv_callback, xfer);

    orte_process_name_t target;
    rc = orcm_cfgi_base_get_hostname_proc(node, &target);
    if (ORCM_SUCCESS != rc){
        orcm_octl_error("node-notfound", node);
        return rc;
    }

    rc = orte_rml.send_buffer_nb(&target, buf, ORCM_RML_TAG_CMD_SERVER,
                                 orte_rml_send_callback, NULL);
    if (ORTE_SUCCESS != rc){
        orcm_octl_error("connection-fail");
        return rc;
    }

    return rc;
}

static int unpack_responses_count(orte_rml_recv_cb_t *xfer){
    int rc = ORCM_SUCCESS;
    int responses_count = 0;
    int elements = 1;

    rc = opal_dss.unpack(&xfer->data, &responses_count, &elements, OPAL_INT);
    if (OPAL_SUCCESS != rc){
        orcm_octl_error("unpack");
        return -1;
    }

    return responses_count;
}

static int unpack_response(char* node, orte_rml_recv_cb_t *xfer){
    int rc = ORCM_SUCCESS;
    int response = ORCM_SUCCESS;
    int elements = 1;

    rc = opal_dss.unpack(&xfer->data, &response, &elements, OPAL_INT);
    if (OPAL_SUCCESS != rc){
        orcm_octl_error("unpack");
        return rc;
    }

    if (ORCM_SUCCESS != response){
        orcm_octl_error("chassis-id-status", node);
        return response;
    }

    return rc;
}

static int unpack_state(char* node, orte_rml_recv_cb_t *xfer){

    int elements = 1;
    int state = -1;
    int rc = opal_dss.unpack(&xfer->data, &state, &elements, OPAL_INT);
    if (OPAL_SUCCESS != rc){
        orcm_octl_error("unpack");
        return rc;
    }

    switch (state){
        case LED_OFF:
            printf("%-10s        %10s \n", node, "OFF");
            break;
        case LED_INDEFINITE_ON:
            printf("%-10s        %10s \n", node, "ON");
            break;
        case LED_TEMPORARY_ON:
            printf("%-10s        %10s \n", node, "TEMPORARY_ON");
            break;
        default:
            printf("%-10s        %10s \n", node, "ERROR");
            break;
    }

    return rc;
}

int pack_chassis_id_data(opal_buffer_t *buf, orcm_cmd_server_flag_t *command,
        orcm_cmd_server_flag_t *sub_command, char **noderaw, unsigned char *seconds){
    int rc = OPAL_SUCCESS;
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, command, 1, ORCM_CMD_SERVER_T))){
        return rc;
    }

    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, sub_command, 1, ORCM_CMD_SERVER_T))){
        return rc;
    }

    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, noderaw, 1, OPAL_STRING))){
        return rc;
    }

    if (ORCM_SET_CHASSIS_ID_TEMPORARY_ON == *sub_command){
        rc = opal_dss.pack(buf, seconds, 1, OPAL_INT);
    }

    return rc;
}

int orcm_octl_led_operation(orcm_cmd_server_flag_t command,
        orcm_cmd_server_flag_t sub_command, char *noderaw, unsigned char seconds){
    orte_rml_recv_cb_t *xfer = NULL;
    opal_buffer_t *buf = NULL;
    int rc = ORCM_SUCCESS;
    char current_aggregator[256];
    int responses = 0;
    int iter = 0;

    if (NULL == (buf = OBJ_NEW(opal_buffer_t)) ||
        NULL == (xfer = OBJ_NEW(orte_rml_recv_cb_t))){
        rc = ORCM_ERR_OUT_OF_RESOURCE;
        cleanup(buf, xfer);
        return rc;
    }

    rc = pack_chassis_id_data(buf, &command, &sub_command, &noderaw, &seconds);
    if (OPAL_SUCCESS != rc){
        orcm_octl_error("pack");
        return rc;
    }

    if (ORTE_SUCCESS != (rc = open_parser_framework())){
        orcm_octl_error("framework-open");
        return rc;
    }

    load_ipmi_config_file();
    close_parser_framework();

    if (ORCM_GET_CHASSIS_ID == command)
        orcm_octl_info("chassis-header");

    while(get_next_aggregator_name(current_aggregator)){
        OBJ_RETAIN(buf);
        OBJ_RETAIN(xfer);

        if (ORCM_SUCCESS != begin_transaction(current_aggregator, buf, xfer)){
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_CMD_SERVER);
            continue;
        }

        ORCM_WAIT_FOR_COMPLETION(xfer->active, ORCM_OCTL_WAIT_TIMEOUT, &rc);
        if (ORCM_SUCCESS != rc) {
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_CMD_SERVER);
            continue;
        }

        responses = unpack_responses_count(xfer);
        if (0 > responses){
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_CMD_SERVER);
            continue;
        }

        for (iter=0; iter<responses; ++iter){
            if (ORCM_SUCCESS != unpack_response(current_aggregator, xfer)){
                orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_CMD_SERVER);
                continue;
            }

            if (ORCM_GET_CHASSIS_ID == command &&
                ORCM_SUCCESS != unpack_state(current_aggregator, xfer)){
                orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_CMD_SERVER);
                continue;
            }
        }
    }

    cleanup(buf, xfer);
    return rc;
}

int orcm_octl_chassis_id_state(char **argv){
    int rc = ORCM_SUCCESS;
    if (MIN_CHASSIS_ID_ARG != opal_argv_count(argv)){
        rc = ORCM_ERR_BAD_PARAM;
        orcm_octl_usage("chassis-id-state", INVALID_USG);
        return rc;
    }

    return orcm_octl_led_operation(ORCM_GET_CHASSIS_ID, ORCM_GET_CHASSIS_ID_STATE, argv[2], 0);
}

int orcm_octl_chassis_id_off(char **argv){
    int rc = ORCM_SUCCESS;
    if (MIN_CHASSIS_ID_ARG != opal_argv_count(argv)){
        rc = ORCM_ERR_BAD_PARAM;
        orcm_octl_usage("chassis-id-disable", INVALID_USG);
        return rc;
    }
    return orcm_octl_led_operation(ORCM_SET_CHASSIS_ID, ORCM_SET_CHASSIS_ID_OFF, argv[2], 0);
}

int orcm_octl_chassis_id_on(char **argv){
    int rc = ORCM_SUCCESS;
    int argv_count = opal_argv_count(argv);
    if (MIN_CHASSIS_ID_ARG == argv_count)
        rc = orcm_octl_led_operation(ORCM_SET_CHASSIS_ID, ORCM_SET_CHASSIS_ID_ON, argv[2], 0);
    else if (MAX_CHASSIS_ID_ARG == argv_count)
        rc = orcm_octl_led_operation(ORCM_SET_CHASSIS_ID, ORCM_SET_CHASSIS_ID_TEMPORARY_ON, argv[3], (unsigned char)atoi(argv[2]));
    else {
        rc = ORCM_ERR_BAD_PARAM;
        orcm_octl_usage("chassis-id-enable", INVALID_USG);
    }
    return rc;
}

static int open_parser_framework(){
    int rc = ORCM_SUCCESS;

    if (ORTE_SUCCESS != (rc = mca_base_framework_open(&orcm_parser_base_framework, (mca_base_open_flag_t)0))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }

    if (ORTE_SUCCESS != (rc = orcm_parser_base_select())) {
        ORTE_ERROR_LOG(rc);
        (void) mca_base_framework_close(&orcm_parser_base_framework);
        return rc;
    }

    return rc;
}

static void close_parser_framework(){
    (void) mca_base_framework_close(&orcm_parser_base_framework);
}
