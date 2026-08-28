/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2026      Amazon.com, Inc. or its affiliates.
 *                         All Rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

/*
 * opal_show_help() renders a message by running a lexer over the help file,
 * and that lexer keeps all of its state in file-scope globals.  Without a lock
 * around it, one thread tearing the scanner down while another is still inside
 * it walks a freed buffer stack: before that was serialized this test
 * segfaulted rather than failing.
 *
 * Look the same topic up from several threads at once and check that every
 * lookup returns the message it asked for.
 */

#include "opal_config.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "support.h"

#include "opal/constants.h"
#include "opal/runtime/opal.h"
#include "opal/util/opal_getcwd.h"
#include "opal/util/show_help.h"

#define NUM_THREADS 8
#define NUM_LOOKUPS 400

/* written next to the test at run time so the test does not depend on an
   installed data directory */
#define HELP_FILE  "opal_show_help_threads_data.txt"
#define HELP_TOPIC "threaded-lookup"
/* has to survive the round trip through the lexer intact */
#define HELP_MARKER "a message only this topic carries"

static volatile int threads_may_start = 0;
static long bad_lookups[NUM_THREADS];

static void *lookup_loop(void *arg)
{
    long id = (long) (intptr_t) arg;
    int i;

    /* pile into the lexer together rather than trickling through it */
    while (0 == threads_may_start) {
        continue;
    }

    for (i = 0; i < NUM_LOOKUPS; ++i) {
        char *str = opal_show_help_string(HELP_FILE, HELP_TOPIC, 0);

        if (NULL == str || NULL == strstr(str, HELP_MARKER)) {
            ++bad_lookups[id];
        }
        if (NULL != str) {
            free(str);
        }
    }

    return NULL;
}

static int write_help_file(void)
{
    FILE *fp = fopen(HELP_FILE, "w");

    if (NULL == fp) {
        return OPAL_ERROR;
    }
    fprintf(fp, "# -*- text -*-\n");
    fprintf(fp, "# a help file written by the test that reads it\n");
    fprintf(fp, "[%s]\n", HELP_TOPIC);
    fprintf(fp, "%s\n", HELP_MARKER);
    fprintf(fp, "and a second line, so the topic is more than one token\n");
    fclose(fp);

    return OPAL_SUCCESS;
}

int main(int argc, char *argv[])
{
    pthread_t threads[NUM_THREADS];
    long total_bad = 0;
    int i, ret;

    test_init("opal_show_help_threads()");

    if (OPAL_SUCCESS != opal_init_util(&argc, &argv)) {
        test_failure("opal_init_util() failed");
        return test_finalize();
    }

    if (OPAL_SUCCESS != write_help_file()) {
        test_failure("could not write the help file");
        opal_finalize_util();
        return test_finalize();
    }

    /* The file lives in the directory the test runs in, not in the install
       tree, so make that directory searchable.  It has to be named absolutely:
       show_help joins a search directory to the file name with
       opal_os_path(false, ...), which always produces an absolute path, so a
       relative directory would be looked for under the root. */
    char cwd[OPAL_PATH_MAX];
    if (OPAL_SUCCESS != opal_getcwd(cwd, sizeof(cwd))) {
        test_failure("opal_getcwd() failed");
        opal_finalize_util();
        unlink(HELP_FILE);
        return test_finalize();
    }
    if (OPAL_SUCCESS != opal_show_help_add_dir(cwd)) {
        test_failure("opal_show_help_add_dir() failed");
        opal_finalize_util();
        unlink(HELP_FILE);
        return test_finalize();
    }

    /* fail loudly rather than silently passing if the file cannot be read at
       all, which would make every thread agree on the wrong answer */
    char *probe = opal_show_help_string(HELP_FILE, HELP_TOPIC, 0);
    if (NULL == probe || NULL == strstr(probe, HELP_MARKER)) {
        test_failure("single-threaded lookup did not find the topic");
        free(probe);
        opal_finalize_util();
        unlink(HELP_FILE);
        return test_finalize();
    }
    free(probe);
    test_success();

    for (i = 0; i < NUM_THREADS; ++i) {
        bad_lookups[i] = 0;
        ret = pthread_create(&threads[i], NULL, lookup_loop, (void *) (intptr_t) i);
        if (0 != ret) {
            test_failure("pthread_create() failed");
            opal_finalize_util();
            unlink(HELP_FILE);
            return test_finalize();
        }
    }

    threads_may_start = 1;

    for (i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    for (i = 0; i < NUM_THREADS; ++i) {
        total_bad += bad_lookups[i];
    }

    if (0 == total_bad) {
        test_success();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "%ld of %d concurrent lookups returned the wrong message",
                 total_bad, NUM_THREADS * NUM_LOOKUPS);
        test_failure(msg);
    }

    unlink(HELP_FILE);
    opal_finalize_util();

    return test_finalize();
}
