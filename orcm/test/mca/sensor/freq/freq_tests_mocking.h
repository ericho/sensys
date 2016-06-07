/*
 * Copyright (c) 2016  Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef FREQ_TESTS_MOCKING_H
#define FREQ_TESTS_MOCKING_H


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
    #include <sys/types.h>
    #include <dirent.h>
    #include <stdio.h>

    extern DIR* __real_opendir(const char*);
    extern int __real_closedir(DIR*);
    extern struct dirent* __real_readdir(DIR*);
#ifdef __cplusplus
}
#endif // __cplusplus

typedef DIR* (*opendir_callback_fn_t)(const char*);
typedef int (*closedir_callback_fn_t)(DIR*);
typedef struct dirent* (*readdir_callback_fn_t)(DIR*);

class freq_tests_mocking
{
    public:
        // Construction
        freq_tests_mocking();

        // Public Callbacks
        opendir_callback_fn_t opendir_callback;
        closedir_callback_fn_t closedir_callback;
        readdir_callback_fn_t readdir_callback;
};

extern freq_tests_mocking freq_mocking;

#endif // FREQ_TESTS_MOCKING_H