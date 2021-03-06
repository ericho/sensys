/*
 * Copyright (c) 2016-2017  Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef UDSENSORS_H
#define UDSENSORS_H

#include "dataContainer.hpp"

// The user should call this macro to expose the plugin to SenSys
// An example on the usage will be:
//
// export_plugin(mySensor, "MySensor");
//
// Where mySensor is an instance derived from UDSensor class and
// "MySensor" is the name which will be used to identify this plugin.
#define export_plugin(PLUGINCLASS, PLUGINNAME)                    \
    extern "C" {                                                  \
        UDSensor* initPlugin()                                    \
        {                                                         \
            return new PLUGINCLASS();                             \
        }                                                         \
        const char *pluginName = PLUGINNAME;                      \
        const char* getPluginName()                               \
        {                                                         \
            return pluginName;                                    \
        }                                                         \
    }                                                             \

// The sensor types determine if the plugin is collecting data via
// in-band(IB) or out-of-band(OOB). Note that OOB sensors run actively
// only in the aggregator node.
enum supportedTypes
{
    IB,
    OOB
};

class UDSensor
{
public:

    // By default, sensor_type is set to IB and can be changed in the
    // constructor of the plugin class.
    supportedTypes sensor_type;
    UDSensor():sensor_type(IB) {};
    virtual ~UDSensor(){};

    // The init function will be called in the initialization process
    // of the plugin. Here is the place where the setup is performed,
    // E. g. look for some resource if it's available, check existance
    // of sysfs entries, etc.
    virtual void init(void) {return;};

    // The finalize function is called when shutdown process of the
    // sensor has started. This can happen when a disable procedure
    // has been called from the SenSys monitoring system.
    virtual void finalize(void) {return;};

    // The sample function is the responsible to gather the sensor
    // samples. The SenSys Monitoring system will call this function
    // in the sample-rate configured for the entire system.
    //
    // A dataContainer object is passed as reference to this function
    // to be populated with the desired samples. For example, to store
    // an integer and a float value into the dataContainer object the
    // user should do the following:
    //
    // void sample(dataContainer &cnt) {
    //     cnt->put("MyIntValue", 10, "ints");
    //     cnt->put("MyFloatValue", 3.1415, "floats");
    // }
    //
    // It is important to note that SenSys expects that the sampling
    // procedure do not take much time to complete, in order to avoid
    // delays in other sensor sampling functions.
    virtual void sample(dataContainer &cnt) {return;};

    // The configuration path is passed as a string in case the plugin
    // wants to obtain useful information from it.
    // E. g. authetication information.
    void setConfigFilePath(std::string path){this->configPath = path;};
    std::string getConfigFilePath(void) {return this->configPath;};

    private:
    std::string configPath;
};

#endif
