/*
 * Copyright (c) 2014-2016  Intel Corporation.  All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
#include "orcm_config.h"
#include "orcm/constants.h"

#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "opal/util/argv.h"
#include "opal/util/output.h"

#include "orte/mca/errmgr/errmgr.h"

#include "orcm/util/cli.h"

#define BONK putchar('\a');

static int make_opt(orcm_cli_t *cli, orcm_cli_init_t *e);
static int make_opt_subtree(orcm_cli_cmd_t *cmd, orcm_cli_init_t *e, int level);
static void print_subtree(orcm_cli_cmd_t *command, int level);
static int get_completions(orcm_cli_t *cli, char **input,
                           char ***completions, opal_list_t *options);
static int get_completions_subtree(orcm_cli_cmd_t *cmd, char **input,
                                   char ***completions, opal_list_t *options);
static int print_completions(orcm_cli_t *cli, char **input);
static int print_completions_subtree(orcm_cli_cmd_t *cmd, char **input);
static void set_handler_default(int sig);
static int orcm_cli_handle_auto_completion(orcm_cli_t *cli, char *input, size_t *len,
                                           bool *space, char *prompt);
static void orcm_cli_handle_space(char c, char *input, size_t *len, bool *space);
static void orcm_cli_handle_backspace(size_t *len, char *input, bool *space);
static void orcm_cli_handle_help_key(orcm_cli_t *cli, char *input, char *prompt);
static void orcm_cli_handle_arrows(int *scroll_indx, char *input, size_t *len, char *prompt);
static void orcm_cli_handle_all_other_keys(char c, char *input, size_t *len, bool *space);
static void add_char_to_input_array(char c, char *input, size_t *len);
static void scroll_up(int *scroll_indx, char *input, size_t *len, char *prompt);
static void scroll_down(int *scroll_indx, char *input, size_t *len, char *prompt);
static void save_cmd_history(char *input);
static struct termios get_initial_term_settings(void);
static void set_term_settings(struct termios settings);
static void restore_term_settings(struct termios initial_settings);

#define CLI_HISTORY_SIZE 15

typedef struct {
    char hist[CLI_HISTORY_SIZE][ORCM_MAX_CLI_LENGTH];
    int count;
    int indx;
} cli_cmd_hist_t;

static cli_cmd_hist_t cmd_hist = {{"0"},0, 0};

int orcm_cli_create(orcm_cli_t *cli,
                    orcm_cli_init_t *table)
{
    int i;
    int rc;

    if (NULL == cli) {
        return ORCM_ERR_BAD_PARAM;
    }

    /* Ensure we got a table */

    if (NULL == table) {
        return ORCM_SUCCESS;
    }

    /* Loop through the table */
    for (i = 0; ; ++i) {

        /* Is this the end? */
        if (NULL == table[i].parent[0] &&
            NULL == table[i].cmd) {
            break;
        }

        /* Nope -- it's an entry.  Process it. */

        if (ORCM_SUCCESS != (rc = make_opt(cli, &table[i]))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }
    }

    return ORCM_SUCCESS;

}

static int make_opt(orcm_cli_t *cli, orcm_cli_init_t *e)
{
    orcm_cli_cmd_t *cmd, *parent;

    /* if the parent is not NULL, then look for the matching
     * parent in the command tree and put this one under it */
    if (NULL != e->parent[0]) {
        OPAL_LIST_FOREACH(parent, &cli->cmds, orcm_cli_cmd_t) {
            if (0 == strcasecmp(parent->cmd, e->parent[0])) {
                /* recursive search down the subtree */
                return make_opt_subtree(parent, e, 1);
            }
        }
        return ORCM_ERR_NOT_FOUND;
    } else {
        /* just add it to the top-level cmd tree */
        cmd = OBJ_NEW(orcm_cli_cmd_t);
        if (NULL != e->cmd) {
            cmd->cmd = strdup(e->cmd);
        }
        if (NULL != e->help) {
            cmd->help = strdup(e->help);
        }
        opal_list_append(&cli->cmds, &cmd->super);
    }
    return ORCM_SUCCESS;
}

static int make_opt_subtree(orcm_cli_cmd_t *cmd, orcm_cli_init_t *e, int level) {
    orcm_cli_cmd_t *newcmd, *parent;
    opal_value_t *kv;

    /* we will keep descending down the subtree until we have nothing
     * more on the parent list to compare to */
    if (NULL == e->parent[level]) {
        /* if this is an option, put it there */
        if (e->option) {
            kv = OBJ_NEW(opal_value_t);
            kv->key = strdup(e->cmd);
            kv->type = OPAL_INT;
            kv->data.integer = e->nargs;
            opal_list_append(&cmd->options, &kv->super);
        } else {
            /* add the commnad in the correct place */
            newcmd = OBJ_NEW(orcm_cli_cmd_t);
            if (NULL != e->cmd) {
                newcmd->cmd = strdup(e->cmd);
            }
            if (NULL != e->help) {
                newcmd->help = strdup(e->help);
            }
            opal_list_append(&cmd->subcmds, &newcmd->super);
        }
        return ORCM_SUCCESS;
    } else {
        /* have more work to do, descend down subtree */
        OPAL_LIST_FOREACH(parent, &cmd->subcmds, orcm_cli_cmd_t) {
            if (0 == strcasecmp(parent->cmd, e->parent[level])) {
                return make_opt_subtree(parent, e, level+1);
            }
        }
        return ORCM_ERR_NOT_FOUND;
    }
}

int orcm_cli_get_cmd(char *prompt,
                     orcm_cli_t *cli,
                     char **cmd)
{
    char c;
    struct termios initial_settings;
    char input[ORCM_MAX_CLI_LENGTH];
    bool space;
    size_t j;
    int rc = ORCM_SUCCESS;
    int scroll_indx = cmd_hist.count;

    /* prep the stack */
    memset(input, 0, ORCM_MAX_CLI_LENGTH);
    j = 0;
    space = false;

    /* Set term settings */
    initial_settings = get_initial_term_settings();
    set_term_settings(initial_settings);

    /* output the prompt */
    fprintf(stdout, "%s> ", prompt);
    /* process command input */
    while ('\n' != (c = getchar())) {
        switch (c) {
        case '\t':   // auto-completion
            rc = orcm_cli_handle_auto_completion(cli, input, &j, &space, prompt);
            if (ORCM_SUCCESS != rc && ORCM_ERR_NOT_FOUND != rc) {
                goto process;
            }
            break;
        case ' ': // space
            orcm_cli_handle_space(c, input, &j, &space);
            break;
        case '\b':
        case 0x7f: // backspace
            orcm_cli_handle_backspace(&j, input, &space);
            break;
        case 0x1b: // arrows
            orcm_cli_handle_arrows(&scroll_indx, input, &j, prompt);
            break;
        case '?':
            orcm_cli_handle_help_key(cli, input, prompt);
            break;
        default: // everything else
            orcm_cli_handle_all_other_keys(c, input, &j, &space);
            rc = ORCM_SUCCESS;
            break;
        }
    }

 process:
    printf("\n");
    restore_term_settings(initial_settings);

    /* return the assembled command */
    *cmd = strdup(input);
    save_cmd_history(input);
    return rc;
}

static void initialize_term_settings(struct termios *initial_settings)
{
    int idx = 0;
    initial_settings->c_iflag = 0;
    initial_settings->c_oflag = 0;
    initial_settings->c_cflag = 0;
    initial_settings->c_lflag = 0;
    initial_settings->c_line = '0';
    for (; idx < NCCS; idx++) {
        initial_settings->c_cc[idx] = '0';
    }
    initial_settings->c_ispeed = 0;
    initial_settings->c_ospeed = 0;
}

struct termios get_initial_term_settings(void)
{
    struct termios initial_settings;
    initialize_term_settings(&initial_settings);
    int rc = 0;
    if (isatty(STDIN_FILENO)) {
        rc = tcgetattr(STDIN_FILENO, &initial_settings);
        if (0 != rc) {
            fprintf(stderr, "Cannot read attributes from stdin\n");
            exit(errno);
        }
    }
    return initial_settings;
}

void restore_term_settings(struct termios initial_settings)
{
    if (isatty(STDIN_FILENO)) {
        tcsetattr(STDIN_FILENO, TCSANOW, &initial_settings);
    }
}

void set_term_settings(struct termios settings)
{
    set_handler_default(SIGTERM);
    set_handler_default(SIGINT);
    set_handler_default(SIGHUP);
    set_handler_default(SIGPIPE);
    set_handler_default(SIGCHLD);

    /* Set the console mode to no-echo, raw input. */
    settings.c_cc[VTIME] = 1;
    settings.c_cc[VMIN] = 1;
    settings.c_iflag &= ~(IXOFF);
    settings.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &settings);
}

void orcm_cli_handle_space(char c,
                           char *input,
                           size_t *len,
                           bool *space)
{
    if (0 == *len || *space) {
        /* if there is already a space on the input line, Bonk */
        BONK;
        return;
    }
    add_char_to_input_array(c, input, len);
    *space = true;
}

void orcm_cli_handle_backspace(size_t *len,
                               char *input,
                               bool *space)
{
    if (0 == *len) {
        /* error - bonk */
        BONK;
        *space = false;
        return;
    }
    /* echo the erase sequence */
    putchar('\b'); putchar(' '); putchar('\b');
    /* remove the last character from input */
    (*len)--;
    *(input + *len) = '\0';
    /* check to see if we already have a space */
    if (0 < *len && ' ' == *(input + *len - 1)) {
        *space = true;
    } else {
        *space = false;
    }
}

void orcm_cli_handle_arrows(int *scroll_indx,
                            char *input,
                            size_t *len,
                            char *prompt)
{
    char c;
    c = getchar();
    switch(c) {
    case '[':
        c = getchar();
        switch(c) {
        case 'A':
            /* up arrow */
            scroll_up(scroll_indx, input, len, prompt);
            break;
        case 'B':
            /* down arrow */
            scroll_down(scroll_indx, input, len, prompt);
            break;
        case 'C':
            /* right arrow */
        case 'D':
            /* left arrow */
        default:
            break;
        }
    default:
        break;
    }
}

void orcm_cli_handle_help_key(orcm_cli_t *cli,
                              char *input,
                              char *prompt)
{
    char **inputlist = NULL;
    inputlist = opal_argv_split(input, ' ');
    printf("\nPossible commands:\n");
    if(ORCM_ERR_NOT_FOUND == print_completions(cli, inputlist)) {
        printf("%s: command not found...\n", input);
    }
    printf("\n%s> %s", prompt, input);

    if (NULL != inputlist) {
        opal_argv_free(inputlist);
    }
}

void orcm_cli_handle_all_other_keys(char c,
                                    char *input,
                                    size_t *len,
                                    bool *space)
{
    *space = false;
    if (ORCM_MAX_CLI_LENGTH == *len) {
        /* can't go that far - bonk */
        BONK;
        return;
    }
    add_char_to_input_array(c, input, len);
}

int orcm_cli_handle_auto_completion(orcm_cli_t *cli,
                                    char *input,
                                    size_t *len,
                                    bool *space,
                                    char *prompt)
{
    char **inputlist = NULL;
    char **completions = NULL;
    opal_value_t *kv;
    opal_list_t *options = NULL;
    int rc = ORCM_SUCCESS;
    size_t k;
    char *tmp = NULL;
    /* break command line so far into array of strings */
    inputlist = opal_argv_split(input, ' ');
    /* get possible completions for the commandline so far */
    rc = get_completions(cli, inputlist, &completions, options);
    if (ORCM_ERR_NOT_FOUND == rc) {
        /* Bad command not in tree, Bonk */
        BONK;
        opal_argv_free(inputlist);
        opal_argv_free(completions);
        return ORCM_SUCCESS;
    } else if (ORCM_SUCCESS == rc) {
        if (NULL == completions) {
            if (NULL == options || opal_list_is_empty(options)) {
                /* no completions and no options, Bonk */
                BONK;
                opal_argv_free(inputlist);
                return ORCM_SUCCESS;
            } else {
                /* no completions, list options */
                printf("\nOPTIONS: ");
                OPAL_LIST_FOREACH(kv, options, opal_value_t) {
                    printf("%s  ", kv->key);
                }
                printf("\n%s> %s", prompt, input);
            }
        } else if (1 == opal_argv_count(completions)) {
            /* only 1 possible completion, go ahead and complete it */
            opal_argv_free(inputlist);
            inputlist = opal_argv_split(input, ' ');
            if (NULL == inputlist) {
                BONK;
                opal_argv_free(completions);
                return ORCM_ERR_NOT_FOUND;
            }
            if (' ' == *(input + *len -1)) {
                k = 0;
            } else if (0 != strncmp(inputlist[(opal_argv_count(inputlist) - 1)],
                                    completions[0],
                                    strlen(inputlist[(opal_argv_count(inputlist) - 1)]))) {
                add_char_to_input_array(' ', input, len);
                k = 0;
            } else {
                k = strlen(inputlist[(opal_argv_count(inputlist) - 1)]);
            }
            for (; k < strlen(completions[0]); k++) {
                add_char_to_input_array(completions[0][k], input, len);
            }
            add_char_to_input_array(' ', input, len);
            *space = true;
        } else {
            /* multiple completions, list possibilities */
            tmp = opal_argv_join(completions, ' ');
            printf("\n\t%s", tmp);
            free(tmp);
            printf("\n%s> %s", prompt, input);
        }
    }

    opal_argv_free(completions);
    opal_argv_free(inputlist);

    return rc;
}

void add_char_to_input_array(char c, char *input, size_t *len)
{
    if (ORCM_MAX_CLI_LENGTH > *len + 1) {
        putchar(c);
        *(input + *len) = c;
        *(input + *len + 1) = '\0';
        (*len)++;
    }
}

static int get_completions(orcm_cli_t *cli, char **input,
                           char ***completions, opal_list_t *options)
{
    orcm_cli_cmd_t *sub_command;
    int i;
    bool found = false;

    i = opal_argv_count(input);
    /* no input, must be at top level
     * add all top level commands to compeltion list */
    if (0 == opal_argv_count(input)) {
        OPAL_LIST_FOREACH(sub_command, &cli->cmds, orcm_cli_cmd_t) {
            opal_argv_append_nosize(completions, sub_command->cmd);
        }
        return ORCM_SUCCESS;
    }

    /* look for top level command to descend into subtree with */
    OPAL_LIST_FOREACH(sub_command, &cli->cmds, orcm_cli_cmd_t) {
        if (0 == strncmp(sub_command->cmd, input[0], strlen(input[0]))) {
            /* if we match our input so far with a command... */
            if (strlen(input[0]) == strlen(sub_command->cmd)) {
                /* if input fully matches command descend */
                opal_argv_delete(&i, &input, 0, 1);
                /* recursive command to search subtree */
                return get_completions_subtree(sub_command, input,
                                               completions, options);
            } else {
                /* otherwise add this to our list of possible completions */
                opal_argv_append_nosize(completions, sub_command->cmd);
                found = true;
            }
        }
    }
    if (!found) {
        return ORCM_ERR_NOT_FOUND;
    }
    return ORCM_SUCCESS;
}

static int get_completions_subtree(orcm_cli_cmd_t *cmd, char **input,
                                   char ***completions, opal_list_t *options)
{
    orcm_cli_cmd_t *sub_command;
    int i;
    bool found = false;

    i = opal_argv_count(input);
    /* nothing on the input line passed to us
     * append all commands at this level */
    if (0 == i) {
        if (opal_list_is_empty(&cmd->subcmds)) {
            options = &cmd->options;
            if (NULL != *completions) {
                opal_argv_free(*completions);
            }
        } else {
            OPAL_LIST_FOREACH(sub_command, &cmd->subcmds, orcm_cli_cmd_t) {
                opal_argv_append_nosize(completions, sub_command->cmd);
            }
        }
        return ORCM_SUCCESS;
    }

    /* look for commands to match input with */
    OPAL_LIST_FOREACH(sub_command, &cmd->subcmds, orcm_cli_cmd_t) {
        if (0 == strncmp(sub_command->cmd, input[0], strlen(input[0]))) {
            /* if we match our input so far with a command... */
            if (strlen(input[0]) == strlen(sub_command->cmd)) {
                /* if input fully matches command descend recursively */
                opal_argv_delete(&i, &input, 0, 1);
                return get_completions_subtree(sub_command, input,
                                               completions, options);
            } else {
                /* otherwise add this to our list of possible completions */
                opal_argv_append_nosize(completions, sub_command->cmd);
                found = true;
            }
        }
    }
    if (!found) {
        return ORCM_ERR_NOT_FOUND;
    }
    return ORCM_SUCCESS;
}

static int print_completions(orcm_cli_t *cli, char **input)
{
    orcm_cli_cmd_t *sub_command;
    int i;
    bool found = false;

    i = opal_argv_count(input);
    /* if no input, print help for all commands at top level */
    if (0 == opal_argv_count(input)) {
        OPAL_LIST_FOREACH(sub_command, &cli->cmds, orcm_cli_cmd_t) {
            orcm_cli_print_cmd(sub_command, NULL);
        }
        return ORCM_SUCCESS;
    }

    /* search for matching commands */
    OPAL_LIST_FOREACH(sub_command, &cli->cmds, orcm_cli_cmd_t) {
        if (0 == strncmp(sub_command->cmd, input[0], strlen(input[0]))) {
            /* if we match our input so far with a command... */
            if (strlen(input[0]) == strlen(sub_command->cmd)) {
                /* if input fully matches command descend */
                opal_argv_delete(&i, &input, 0, 1);
                return print_completions_subtree(sub_command, input);
            } else {
                /* otherwise print help for possible completions */
                orcm_cli_print_cmd(sub_command, NULL);
                found = true;
            }
        }
    }
    if (!found) {
        return ORCM_ERR_NOT_FOUND;
    }
    return ORCM_SUCCESS;

}

static int print_completions_subtree(orcm_cli_cmd_t *cmd, char **input)
{
    orcm_cli_cmd_t *sub_command;
    int i;
    bool found = false;

    i = opal_argv_count(input);
    /* if no input, print help for all commands at this level */
    if (0 == i) {
        OPAL_LIST_FOREACH(sub_command, &cmd->subcmds, orcm_cli_cmd_t) {
            orcm_cli_print_cmd(sub_command, NULL);
        }
        return ORCM_SUCCESS;
    }

    OPAL_LIST_FOREACH(sub_command, &cmd->subcmds, orcm_cli_cmd_t) {
        if (0 == strncmp(sub_command->cmd, input[0], strlen(input[0]))) {
            /* if we match our input so far with a command... */
            if (strlen(input[0]) == strlen(sub_command->cmd)) {
                opal_argv_delete(&i, &input, 0, 1);
                /* if input fully matches command descend recursively */
                return print_completions_subtree(sub_command, input);
            } else {
                /* print help for possible completions */
                orcm_cli_print_cmd(sub_command, NULL);
                found = true;
            }
        }
    }
    if (!found) {
        return ORCM_ERR_NOT_FOUND;
    }
    return ORCM_SUCCESS;
}

void orcm_cli_print_cmd(orcm_cli_cmd_t *cmd, char *prefix)
{
    opal_value_t *kv;
    printf("%-2s %-20s %-10s\n",
           (NULL == prefix) ? "" : prefix,
           (NULL == cmd->cmd) ? "NULL" : cmd->cmd,
           (NULL == cmd->help) ? "NULL" : cmd->help);
    OPAL_LIST_FOREACH(kv, &cmd->options, opal_value_t) {
        printf("\tOPTION: %s nargs %d\n", kv->key, kv->data.integer);
    }
}

void orcm_cli_print_tree(orcm_cli_t *cli)
{
    orcm_cli_cmd_t *sub_command;

    /* recursive set of functions to print the whole command tree */

    OPAL_LIST_FOREACH(sub_command, &cli->cmds, orcm_cli_cmd_t) {
        printf("%s\n",
               (NULL == sub_command->cmd) ? "NULL" : sub_command->cmd);
        print_subtree(sub_command, 1);
    }
}

static void print_subtree(orcm_cli_cmd_t *command, int level)
{
    orcm_cli_cmd_t *sub_command;
    int i;

    OPAL_LIST_FOREACH(sub_command, &command->subcmds, orcm_cli_cmd_t) {
        for (i = 0; i < level; i++) {
            printf("  ");
        }
        /* use special unicode box chars for help displaying tree */
        printf("\u2514\u2500%s\n",
               (NULL == sub_command->cmd) ? "NULL" : sub_command->cmd);
        print_subtree(sub_command, level+1);
    }
}

int orcm_cli_print_cmd_help(orcm_cli_t *cli, char **input)
{
    return print_completions(cli, input);
}

static void set_handler_default(int sig)
{
    struct sigaction act;
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(sig, &act, (struct sigaction *)0);
}

static void scroll_up(int *scroll_indx, char *input, size_t *len, char *prompt)
{
    if (!cmd_hist.count || *scroll_indx < 1) {
        BONK;
        return;
    }

    *scroll_indx = *scroll_indx - 1;
    strcpy(input, cmd_hist.hist[*scroll_indx]);
    *len = strlen(input);
    printf("\033[1K \r%s> ", prompt);
    printf("%s", input);
    return;
}

static void scroll_down(int *scroll_indx, char *input, size_t *len, char *prompt)
{
    printf("\033[1K \r%s> ", prompt);
    if (!cmd_hist.count || cmd_hist.count <= *scroll_indx ) {
        BONK;
        return;
    }
    *scroll_indx = *scroll_indx + 1;
    if (CLI_HISTORY_SIZE == *scroll_indx) {
        input[0] = 0;
    } else {
        strcpy(input, cmd_hist.hist[*scroll_indx]);
    }
    *len = strlen(input);
    printf("%s", input);
    return;
}

static void save_cmd_history(char *input)
{
    int local_index;
    if (0 < strlen(input)) {
        if (CLI_HISTORY_SIZE == cmd_hist.indx) {
            for (local_index = 0; local_index < CLI_HISTORY_SIZE - 1; local_index++ ) {
                strncpy(cmd_hist.hist[local_index],
                        cmd_hist.hist[local_index + 1],
                        sizeof(cmd_hist.hist[local_index]));
            }
            strncpy(cmd_hist.hist[CLI_HISTORY_SIZE - 1],
                    input,
                    sizeof(cmd_hist.hist[CLI_HISTORY_SIZE - 1]));
        } else {
            strcpy (cmd_hist.hist[cmd_hist.indx], input);
            if ((CLI_HISTORY_SIZE - 1) >= cmd_hist.count) {
                cmd_hist.count++;
            }
            if ((CLI_HISTORY_SIZE - 1) >= cmd_hist.indx) {
                cmd_hist.indx++;
            }
        }
    }
}

/***   CLASS INSTANCES   ***/
static void cmdcon(orcm_cli_cmd_t *p)
{
    p->cmd = NULL;
    OBJ_CONSTRUCT(&p->options, opal_list_t);;
    p->help = NULL;
    OBJ_CONSTRUCT(&p->subcmds, opal_list_t);
}
static void cmddes(orcm_cli_cmd_t *p)
{
    if (NULL != p->cmd) {
        free(p->cmd);
    }
    OPAL_LIST_DESTRUCT(&p->options);
    if (NULL != p->help) {
        free(p->help);
    }
    OPAL_LIST_DESTRUCT(&p->subcmds);
}
OBJ_CLASS_INSTANCE(orcm_cli_cmd_t,
                   opal_list_item_t,
                   cmdcon, cmddes);

static void clicon(orcm_cli_t *p)
{
    OBJ_CONSTRUCT(&p->cmds, opal_list_t);
}
static void clides(orcm_cli_t *p)
{
    OPAL_LIST_DESTRUCT(&p->cmds);
}
OBJ_CLASS_INSTANCE(orcm_cli_t,
                   opal_object_t,
                   clicon, clides);
