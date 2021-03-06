/*
 * Copyright (c) 2015-2016  Intel Corporation. All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef SNMP_PARSER_TESTS_H
#define SNMP_PARSER_TESTS_H

#include "gtest/gtest.h"
#include "orcm/mca/sensor/snmp/snmp_parser.h"

#include "snmp_test_files.h"

extern "C"{
    #include "orcm/mca/parser/base/base.h"
    #include "orcm/mca/parser/pugi/parser_pugi.h"
    #include "opal/runtime/opal.h"
}
#include <string.h>
#include <iostream>

#define MY_MODULE_PRIORITY 20

class ut_snmp_parser_tests: public testing::Test {
    protected:
        static void SetUpTestCase();
        static void TearDownTestCase();
        static void initParserFramework();
        static void cleanParserFramework();
        static void removeConfigFile();
        static void writeConfigFile();
        static void writeTestFiles();
        static void removeTestFiles();
};

#endif
