/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@example ex_git.c
 *@brief Nilorea Library n_git module test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/03/2026
 */

#ifdef HAVE_LIBGIT2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_list.h"
#include "nilorea/n_git.h"

void usage(void) {
    fprintf(stderr,
            "     -v version\n"
            "     -V log level: LOG_INFO, LOG_NOTICE, LOG_ERR, LOG_DEBUG\n"
            "     -h help\n");
}

void process_args(int argc, char** argv) {
    int getoptret = 0,
        log_level = LOG_DEBUG;

    while ((getoptret = getopt(argc, argv, "hvV:")) != EOF) {
        switch (getoptret) {
            case 'v':
                fprintf(stderr, "Date de compilation : %s a %s.\n", __DATE__, __TIME__);
                exit(1);
            case 'V':
                if (!strcmp("LOG_NULL", optarg))
                    log_level = LOG_NULL;
                else if (!strcmp("LOG_NOTICE", optarg))
                    log_level = LOG_NOTICE;
                else if (!strcmp("LOG_INFO", optarg))
                    log_level = LOG_INFO;
                else if (!strcmp("LOG_ERR", optarg))
                    log_level = LOG_ERR;
                else if (!strcmp("LOG_DEBUG", optarg))
                    log_level = LOG_DEBUG;
                else {
                    fprintf(stderr, "%s is not a valid log level.\n", optarg);
                    exit(-1);
                }
                break;
            default:
            case '?': {
                if (optopt == 'V') {
                    fprintf(stderr, "\n      Missing log level\n");
                }
                usage();
                exit(1);
            }
            case 'h': {
                usage();
                exit(1);
            }
        }
    }
    set_log_level(log_level);
}

/**
 *@brief Write a file inside the repo with given content.
 *@param dir Repository directory path.
 *@param filename Name of the file to write.
 *@param content Content to write.
 *@return 0 on success, -1 on failure.
 */
static int write_file(const char* dir, const char* filename, const char* content) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    FILE* fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "%s", content);
    fclose(fp);
    return 0;
}

/**
 *@brief Remove a directory tree recursively (simple rm -rf via system).
 *@param dir Path to remove.
 */
static void cleanup_dir(const char* dir) {
    char cmd[1088];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    int ret = system(cmd);
    (void)ret;
}

int main(int argc, char** argv) {
    process_args(argc, argv);

    int errors = 0;

    /* create temp directory */
    char tmpdir[] = "/tmp/nilorea_git_test_XXXXXX";
    if (!mkdtemp(tmpdir)) {
        n_log(LOG_ERR, "mkdtemp failed");
        return 1;
    }
    n_log(LOG_INFO, "Test repo dir: %s", tmpdir);

    /* init repo */
    N_GIT_REPO* repo = n_git_init(tmpdir);
    if (!repo) {
        n_log(LOG_ERR, "FAIL: n_git_init returned NULL");
        cleanup_dir(tmpdir);
        return 1;
    }
    n_log(LOG_INFO, "PASS: n_git_init");

    /* create a file */
    if (write_file(tmpdir, "test.txt", "hello") != 0) {
        n_log(LOG_ERR, "FAIL: could not write test.txt");
        errors++;
        goto done;
    }

    /* stage it */
    if (n_git_stage(repo, "test.txt") != 0) {
        n_log(LOG_ERR, "FAIL: n_git_stage");
        errors++;
        goto done;
    }
    n_log(LOG_INFO, "PASS: n_git_stage");

    /* commit */
    if (n_git_commit(repo, "initial commit", "Test User", "test@example.org") != 0) {
        n_log(LOG_ERR, "FAIL: n_git_commit");
        errors++;
        goto done;
    }
    n_log(LOG_INFO, "PASS: n_git_commit");

    /* verify status is clean */
    {
        LIST* st = n_git_status(repo);
        if (!st) {
            n_log(LOG_ERR, "FAIL: n_git_status returned NULL");
            errors++;
        } else {
            if (st->nb_items != 0) {
                n_log(LOG_ERR, "FAIL: status should be clean, got %zu entries", st->nb_items);
                errors++;
            } else {
                n_log(LOG_INFO, "PASS: status is clean after commit");
            }
            list_destroy(&st);
        }
    }

    /* modify the file */
    if (write_file(tmpdir, "test.txt", "hello world modified") != 0) {
        n_log(LOG_ERR, "FAIL: could not modify test.txt");
        errors++;
        goto done;
    }

    /* get diff (should be non-empty) */
    {
        N_STR* diff = n_git_diff_workdir(repo);
        if (!diff || !diff->data || diff->written == 0) {
            n_log(LOG_ERR, "FAIL: diff should be non-empty after modification");
            errors++;
        } else {
            n_log(LOG_INFO, "PASS: diff is non-empty after modification");
            n_log(LOG_DEBUG, "Diff output:\n%s", diff->data);
        }
        free_nstr(&diff);
    }

    /* restore the file */
    if (n_git_checkout_path(repo, "test.txt") != 0) {
        n_log(LOG_ERR, "FAIL: n_git_checkout_path");
        errors++;
        goto done;
    }
    n_log(LOG_INFO, "PASS: n_git_checkout_path");

    /* verify diff is now empty */
    {
        N_STR* diff = n_git_diff_workdir(repo);
        if (diff && diff->data && diff->written > 0) {
            n_log(LOG_ERR, "FAIL: diff should be empty after restore, got: %s", diff->data);
            errors++;
        } else {
            n_log(LOG_INFO, "PASS: diff is empty after restore");
        }
        free_nstr(&diff);
    }

    /* get log (should have 1 entry) */
    {
        LIST* log_list = n_git_log(repo, 10);
        if (!log_list) {
            n_log(LOG_ERR, "FAIL: n_git_log returned NULL");
            errors++;
        } else {
            if (log_list->nb_items != 1) {
                n_log(LOG_ERR, "FAIL: log should have 1 entry, got %zu", log_list->nb_items);
                errors++;
            } else {
                n_log(LOG_INFO, "PASS: log has 1 entry");
                LIST_NODE* node = log_list->start;
                if (node && node->ptr) {
                    N_GIT_COMMIT_INFO* info = (N_GIT_COMMIT_INFO*)node->ptr;
                    n_log(LOG_INFO, "  hash: %s", info->hash);
                    n_log(LOG_INFO, "  short: %s", info->short_hash);
                    n_log(LOG_INFO, "  msg: %s", info->message);
                    n_log(LOG_INFO, "  author: %s", info->author);
                }
            }
            list_destroy(&log_list);
        }
    }

    /* test subdirectory staging */
    {
        /* create a subdirectory with files */
        char subdir[1088];
        snprintf(subdir, sizeof(subdir), "%s/subdir", tmpdir);
        char mkdircmd[1088 + 16];
        snprintf(mkdircmd, sizeof(mkdircmd), "mkdir -p %s", subdir);
        if (system(mkdircmd) != 0) {
            n_log(LOG_ERR, "FAIL: could not create subdir");
            errors++;
            goto done;
        }
        if (write_file(tmpdir, "subdir/file_a.txt", "aaa") != 0 ||
            write_file(tmpdir, "subdir/file_b.txt", "bbb") != 0) {
            n_log(LOG_ERR, "FAIL: could not write subdir files");
            errors++;
            goto done;
        }

        /* status should report individual files (not collapsed directory) */
        LIST* st = n_git_status(repo);
        if (!st) {
            n_log(LOG_ERR, "FAIL: n_git_status returned NULL for subdir test");
            errors++;
        } else {
            int found_a = 0, found_b = 0;
            list_foreach(node, st) {
                N_GIT_STATUS_ENTRY* e = (N_GIT_STATUS_ENTRY*)node->ptr;
                if (e && strcmp(e->path, "subdir/file_a.txt") == 0) found_a = 1;
                if (e && strcmp(e->path, "subdir/file_b.txt") == 0) found_b = 1;
            }
            if (found_a && found_b) {
                n_log(LOG_INFO, "PASS: status reports individual subdir files");
            } else {
                n_log(LOG_ERR, "FAIL: status did not report individual subdir files (a=%d b=%d)",
                      found_a, found_b);
                errors++;
            }
            list_destroy(&st);
        }

        /* stage using directory path (trailing slash) */
        if (n_git_stage(repo, "subdir/") != 0) {
            n_log(LOG_ERR, "FAIL: n_git_stage with directory path");
            errors++;
        } else {
            n_log(LOG_INFO, "PASS: n_git_stage with directory path");
        }

        /* commit the subdir files */
        if (n_git_commit(repo, "add subdir files", "Test User", "test@example.org") != 0) {
            n_log(LOG_ERR, "FAIL: n_git_commit for subdir");
            errors++;
        } else {
            n_log(LOG_INFO, "PASS: n_git_commit for subdir");
        }

        /* verify status is clean */
        st = n_git_status(repo);
        if (!st) {
            n_log(LOG_ERR, "FAIL: n_git_status returned NULL after subdir commit");
            errors++;
        } else {
            if (st->nb_items != 0) {
                n_log(LOG_ERR, "FAIL: status should be clean after subdir commit, got %zu", st->nb_items);
                errors++;
            } else {
                n_log(LOG_INFO, "PASS: status is clean after subdir commit");
            }
            list_destroy(&st);
        }
    }

    /* test current branch */
    {
        char branch[256];
        if (n_git_current_branch(repo, branch, sizeof(branch)) == 0) {
            n_log(LOG_INFO, "PASS: current branch = %s", branch);
        } else {
            n_log(LOG_ERR, "FAIL: n_git_current_branch");
            errors++;
        }
    }

    /* test branch operations */
    {
        if (n_git_create_branch(repo, "test-branch") == 0) {
            n_log(LOG_INFO, "PASS: n_git_create_branch");
        } else {
            n_log(LOG_ERR, "FAIL: n_git_create_branch");
            errors++;
        }

        LIST* branches = n_git_list_branches(repo);
        if (branches) {
            n_log(LOG_INFO, "PASS: n_git_list_branches (%zu branches)", branches->nb_items);
            list_destroy(&branches);
        } else {
            n_log(LOG_ERR, "FAIL: n_git_list_branches");
            errors++;
        }

        if (n_git_delete_branch(repo, "test-branch") == 0) {
            n_log(LOG_INFO, "PASS: n_git_delete_branch");
        } else {
            n_log(LOG_ERR, "FAIL: n_git_delete_branch");
            errors++;
        }
    }

done:
    n_git_close(&repo);
    cleanup_dir(tmpdir);

    if (errors > 0) {
        n_log(LOG_ERR, "RESULT: %d test(s) FAILED", errors);
        return 1;
    }
    n_log(LOG_INFO, "RESULT: all tests PASSED");
    return 0;
}

#else /* !HAVE_LIBGIT2 */

#include <stdio.h>

int main(void) {
    fprintf(stderr, "ex_git: built without HAVE_LIBGIT2, skipping\n");
    return 0;
}

#endif /* HAVE_LIBGIT2 */
