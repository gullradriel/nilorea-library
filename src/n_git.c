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
 *@file n_git.c
 *@brief libgit2 wrapper for Git repository operations
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/03/2026
 */

#ifdef HAVE_LIBGIT2

/* libgit2 *_INIT macros trigger -Wmissing-field-initializers; suppress here */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "nilorea/n_git.h"
#include "nilorea/n_log.h"
#include "nilorea/n_common.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/*! static flag ensuring git_libgit2_init is called only once */
static int _n_git_initialized = 0;

/**
 *@brief Ensure libgit2 is initialized (idempotent).
 */
static void _n_git_ensure_init(void) {
    if (!_n_git_initialized) {
        git_libgit2_init();
#if LIBGIT2_VER_MAJOR > 1 || (LIBGIT2_VER_MAJOR == 1 && (LIBGIT2_VER_MINOR > 4 || (LIBGIT2_VER_MINOR == 4 && LIBGIT2_VER_REVISION >= 3)))
        /* Disable ownership validation, on Windows, NTFS ACLs often
         * cause false-positive "not owned by current user" errors for
         * directories like %APPDATA% that legitimately belong to the
         * user but were created by the system or an installer. */
        git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);
#endif
        _n_git_initialized = 1;
    }
}

/**
 *@brief Log the last libgit2 error.
 *@param func_name Name of the calling function for context.
 */
static void _n_git_log_error(const char* func_name) {
    const git_error* err = git_error_last();
    if (err) {
        n_log(LOG_ERR, "%s: %s", func_name, err->message);
    } else {
        n_log(LOG_ERR, "%s: unknown libgit2 error", func_name);
    }
}

/**
 *@brief Set the last_status message on a repo handle.
 *@param repo The repo handle (may be NULL).
 *@param fmt printf-style format string.
 */
#if defined(__MINGW32__) || defined(__MINGW64__)
static void _n_git_set_status(N_GIT_REPO* repo, const char* fmt, ...) __attribute__((format(__MINGW_PRINTF_FORMAT, 2, 3)));
#elif defined(__GNUC__) || defined(__clang__)
static void _n_git_set_status(N_GIT_REPO* repo, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
static void _n_git_set_status(N_GIT_REPO* repo, const char* fmt, ...) {
    if (!repo) return;
    repo->last_status[0] = '\0';
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(repo->last_status, sizeof(repo->last_status), fmt, args);
    va_end(args);
    if (ret < 0) {
        repo->last_status[0] = '\0';
    }
    repo->last_status[sizeof(repo->last_status) - 1] = '\0';
}

/**
 *@brief Destructor for N_GIT_STATUS_ENTRY used by LIST.
 *@param ptr Pointer to the entry to free.
 */
static void _n_git_status_entry_destroy(void* ptr) {
    // cppcheck-suppress uselessAssignmentPtrArg ; FreeNoLog nulls local copy, intentional for destroy callback API
    FreeNoLog(ptr);
}

/**
 *@brief Destructor for N_GIT_COMMIT_INFO used by LIST.
 *@param ptr Pointer to the entry to free.
 */
static void _n_git_commit_info_destroy(void* ptr) {
    // cppcheck-suppress uselessAssignmentPtrArg ; FreeNoLog nulls local copy, intentional for destroy callback API
    FreeNoLog(ptr);
}

/**
 *@brief Destructor for char* branch name strings used by LIST.
 *@param ptr Pointer to the string to free.
 */
static void _n_git_string_destroy(void* ptr) {
    // cppcheck-suppress uselessAssignmentPtrArg ; FreeNoLog nulls local copy, intentional for destroy callback API
    FreeNoLog(ptr);
}

/**
 *@brief Callback for git_diff_print, appends each line to an N_STR.
 *@param delta Unused.
 *@param hunk Unused.
 *@param line The diff line to append.
 *@param payload Pointer to the N_STR* accumulator.
 *@return 0 on success.
 */
static int _n_git_diff_print_cb(const git_diff_delta* delta, const git_diff_hunk* hunk, const git_diff_line* line, void* payload) {
    (void)delta;
    (void)hunk;
    N_STR** nstr = (N_STR**)payload;

    /* line->content is not null-terminated; copy it */
    if (line->content && line->content_len > 0) {
        char* tmp = NULL;
        Malloc(tmp, char, line->content_len + 2);
        if (tmp) {
            memcpy(tmp, line->content, line->content_len);
            tmp[line->content_len] = '\0';
            nstrprintf_cat(*nstr, "%c%s", line->origin, tmp);
            FreeNoLog(tmp);
        }
    }
    return 0;
}

/**
 *@brief Open an existing Git repository.
 */
N_GIT_REPO* n_git_open(const char* path) {
    __n_assert(path, return NULL);

    _n_git_ensure_init();

    N_GIT_REPO* ngit = NULL;
    Malloc(ngit, N_GIT_REPO, 1);
    __n_assert(ngit, return NULL);

    ngit->path = strdup(path);
    if (!ngit->path) {
        n_log(LOG_ERR, "n_git_open: strdup failed");
        FreeNoLog(ngit);
        return NULL;
    }

    int err = git_repository_open(&ngit->repo, path);
    if (err < 0) {
        _n_git_log_error("n_git_open");
        FreeNoLog(ngit->path);
        FreeNoLog(ngit);
        return NULL;
    }

    return ngit;
} /* n_git_open */

/**
 *@brief Initialize a new Git repository.
 *@param path Filesystem path where the repository will be created.
 *@return A new N_GIT_REPO handle, or NULL on failure.
 */
N_GIT_REPO* n_git_init(const char* path) {
    return n_git_init_ex(path, NULL);
} /* n_git_init */

/**
 *@brief Initialize a new Git repository with a custom initial branch name.
 *@param path Filesystem path where the repository will be created.
 *@param initial_branch Name of the initial branch (e.g. "main"). If NULL, uses libgit2 default.
 *@return A new N_GIT_REPO handle, or NULL on failure.
 */
N_GIT_REPO* n_git_init_ex(const char* path, const char* initial_branch) {
    __n_assert(path, return NULL);

    _n_git_ensure_init();

    N_GIT_REPO* ngit = NULL;
    Malloc(ngit, N_GIT_REPO, 1);
    __n_assert(ngit, return NULL);

    ngit->path = strdup(path);
    if (!ngit->path) {
        n_log(LOG_ERR, "n_git_init_ex: strdup failed");
        FreeNoLog(ngit);
        return NULL;
    }

    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.flags = GIT_REPOSITORY_INIT_MKPATH;
    if (initial_branch && initial_branch[0]) {
        opts.initial_head = initial_branch;
    }

    int err = git_repository_init_ext(&ngit->repo, path, &opts);
    if (err < 0) {
        _n_git_log_error("n_git_init_ex");
        FreeNoLog(ngit->path);
        FreeNoLog(ngit);
        return NULL;
    }

    return ngit;
} /* n_git_init_ex */

/**
 *@brief Close a Git repository and free resources.
 */
void n_git_close(N_GIT_REPO** repo) {
    __n_assert(repo, return);
    __n_assert(*repo, return);

    if ((*repo)->repo) {
        git_repository_free((*repo)->repo);
        (*repo)->repo = NULL;
    }
    FreeNoLog((*repo)->path);
    if ((*repo)->remote_auth) {
        FreeNoLog((*repo)->remote_auth->username);
        FreeNoLog((*repo)->remote_auth->password);
        FreeNoLog((*repo)->remote_auth->ssh_pubkey_path);
        FreeNoLog((*repo)->remote_auth->ssh_privkey_path);
        FreeNoLog((*repo)->remote_auth->ssh_passphrase);
        FreeNoLog((*repo)->remote_auth);
    }
    FreeNoLog(*repo);
} /* n_git_close */

/**
 *@brief Get the status of all files in the working directory.
 *@param repo An opened N_GIT_REPO handle.
 *@return A LIST of N_GIT_STATUS_ENTRY pointers, or NULL on failure.
 */
LIST* n_git_status(N_GIT_REPO* repo) {
    __n_assert(repo, return NULL);
    __n_assert(repo->repo, return NULL);

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;

    git_status_list* status_list = NULL;
    int err = git_status_list_new(&status_list, repo->repo, &opts);
    if (err < 0) {
        _n_git_log_error("n_git_status");
        return NULL;
    }

    LIST* list = new_generic_list(UNLIMITED_LIST_ITEMS);
    if (!list) {
        git_status_list_free(status_list);
        return NULL;
    }

    size_t count = git_status_list_entrycount(status_list);
    for (size_t i = 0; i < count; i++) {
        const git_status_entry* entry = git_status_byindex(status_list, i);
        if (!entry) continue;

        N_GIT_STATUS_ENTRY* se = NULL;
        Malloc(se, N_GIT_STATUS_ENTRY, 1);
        if (!se) continue;

        const char* fpath = NULL;
        if (entry->head_to_index && entry->head_to_index->new_file.path) {
            fpath = entry->head_to_index->new_file.path;
        } else if (entry->index_to_workdir && entry->index_to_workdir->new_file.path) {
            fpath = entry->index_to_workdir->new_file.path;
        } else if (entry->index_to_workdir && entry->index_to_workdir->old_file.path) {
            fpath = entry->index_to_workdir->old_file.path;
        }

        if (fpath) {
            snprintf(se->path, sizeof(se->path), "%s", fpath);
        }

        se->flags = 0;

        /* index (staged) flags */
        if (entry->status & GIT_STATUS_INDEX_NEW) {
            se->flags |= N_GIT_STATUS_NEW | N_GIT_STATUS_STAGED;
        }
        if (entry->status & GIT_STATUS_INDEX_MODIFIED) {
            se->flags |= N_GIT_STATUS_MODIFIED | N_GIT_STATUS_STAGED;
        }
        if (entry->status & GIT_STATUS_INDEX_DELETED) {
            se->flags |= N_GIT_STATUS_DELETED | N_GIT_STATUS_STAGED;
        }

        /* workdir (unstaged) flags */
        if (entry->status & GIT_STATUS_WT_NEW) {
            se->flags |= N_GIT_STATUS_NEW;
        }
        if (entry->status & GIT_STATUS_WT_MODIFIED) {
            se->flags |= N_GIT_STATUS_MODIFIED;
        }
        if (entry->status & GIT_STATUS_WT_DELETED) {
            se->flags |= N_GIT_STATUS_DELETED;
        }

        list_push(list, se, _n_git_status_entry_destroy);
    }

    git_status_list_free(status_list);
    return list;
} /* n_git_status */

/**
 *@brief Stage a file or directory by path.
 *
 * If filepath ends with '/' it is treated as a directory and all files
 * beneath it are staged via pathspec matching (git_index_add_all).
 * Otherwise a single file is staged via git_index_add_bypath.
 *
 * Paths should use forward slashes as separators (the convention used
 * by libgit2 on all platforms).
 *
 *@param repo An opened N_GIT_REPO handle.
 *@param filepath Relative path of the file or directory to stage.
 *@return 0 on success, -1 on failure.
 */
int n_git_stage(N_GIT_REPO* repo, const char* filepath) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(filepath, return -1);

    git_index* index = NULL;
    int ret = -1;

    int err = git_repository_index(&index, repo->repo);
    if (err < 0) {
        _n_git_log_error("n_git_stage");
        _n_git_set_status(repo, "stage failed: %s", filepath);
        return -1;
    }

    size_t len = strlen(filepath);

    /* directory paths (ending with '/') cannot be staged with
     * git_index_add_bypath; use git_index_add_all with a pathspec instead.
     * Only '/' is checked because libgit2 uses forward slashes on all
     * platforms; on POSIX '\\' is a valid filename character. */
    if (len > 0 && filepath[len - 1] == '/') {
        char* pathspec_str = (char*)filepath;
        git_strarray pathspec;
        pathspec.strings = &pathspec_str;
        pathspec.count = 1;

        err = git_index_add_all(index, &pathspec, GIT_INDEX_ADD_DEFAULT, NULL, NULL);
        if (err < 0) {
            _n_git_log_error("n_git_stage");
            _n_git_set_status(repo, "stage failed: %s", filepath);
            goto cleanup;
        }
    } else {
        err = git_index_add_bypath(index, filepath);
        if (err < 0) {
            _n_git_log_error("n_git_stage");
            _n_git_set_status(repo, "stage failed: %s", filepath);
            goto cleanup;
        }
    }

    err = git_index_write(index);
    if (err < 0) {
        _n_git_log_error("n_git_stage");
        _n_git_set_status(repo, "stage failed: %s", filepath);
        goto cleanup;
    }

    _n_git_set_status(repo, "staged: %s", filepath);
    ret = 0;

cleanup:
    git_index_free(index);
    return ret;
} /* n_git_stage */

/**
 *@brief Stage all modified and untracked files.
 *@param repo An opened N_GIT_REPO handle.
 *@return 0 on success, -1 on failure.
 */
int n_git_stage_all(N_GIT_REPO* repo) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);

    git_index* index = NULL;
    int ret = -1;

    int err = git_repository_index(&index, repo->repo);
    if (err < 0) {
        _n_git_log_error("n_git_stage_all");
        _n_git_set_status(repo, "stage all failed");
        return -1;
    }

    err = git_index_add_all(index, NULL, GIT_INDEX_ADD_DEFAULT, NULL, NULL);
    if (err < 0) {
        _n_git_log_error("n_git_stage_all");
        _n_git_set_status(repo, "stage all failed");
        goto cleanup;
    }

    err = git_index_write(index);
    if (err < 0) {
        _n_git_log_error("n_git_stage_all");
        _n_git_set_status(repo, "stage all failed");
        goto cleanup;
    }

    _n_git_set_status(repo, "all files staged");
    ret = 0;

cleanup:
    git_index_free(index);
    return ret;
} /* n_git_stage_all */

/**
 *@brief Unstage a single file (reset from HEAD).
 *@param repo An opened N_GIT_REPO handle.
 *@param filepath Relative path of the file to unstage.
 *@return 0 on success, -1 on failure.
 */
int n_git_unstage(N_GIT_REPO* repo, const char* filepath) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(filepath, return -1);

    git_reference* head_ref = NULL;
    git_object* head_obj = NULL;
    int ret = -1;

    /* try to get HEAD; for initial commit there may be no HEAD */
    int err = git_repository_head(&head_ref, repo->repo);
    if (err == 0) {
        err = git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT);
        if (err < 0) {
            _n_git_log_error("n_git_unstage");
            _n_git_set_status(repo, "unstage failed: %s", filepath);
            goto cleanup;
        }
    }

    git_strarray paths;
    char* path_str = (char*)filepath;
    paths.strings = &path_str;
    paths.count = 1;

    err = git_reset_default(repo->repo, head_obj, &paths);
    if (err < 0) {
        _n_git_log_error("n_git_unstage");
        _n_git_set_status(repo, "unstage failed: %s", filepath);
        goto cleanup;
    }

    _n_git_set_status(repo, "unstaged: %s", filepath);
    ret = 0;

cleanup:
    if (head_obj) git_object_free(head_obj);
    if (head_ref) git_reference_free(head_ref);
    return ret;
} /* n_git_unstage */

/**
 *@brief Create a commit from the current index.
 *@param repo An opened N_GIT_REPO handle.
 *@param message The commit message.
 *@param author_name Author name string.
 *@param author_email Author email string.
 *@return 0 on success, -1 on failure.
 */
int n_git_commit(N_GIT_REPO* repo, const char* message, const char* author_name, const char* author_email) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(message, return -1);
    __n_assert(author_name, return -1);
    __n_assert(author_email, return -1);

    git_index* index = NULL;
    git_oid tree_oid;
    git_oid commit_oid;
    git_tree* tree = NULL;
    git_signature* sig = NULL;
    git_commit* parent_commit = NULL;
    git_reference* head_ref = NULL;
    int ret = -1;

    int err = git_repository_index(&index, repo->repo);
    if (err < 0) {
        _n_git_log_error("n_git_commit");
        _n_git_set_status(repo, "commit failed: cannot open index");
        return -1;
    }

    err = git_index_write_tree(&tree_oid, index);
    if (err < 0) {
        _n_git_log_error("n_git_commit");
        _n_git_set_status(repo, "commit failed: cannot write tree");
        goto cleanup;
    }

    err = git_tree_lookup(&tree, repo->repo, &tree_oid);
    if (err < 0) {
        _n_git_log_error("n_git_commit");
        _n_git_set_status(repo, "commit failed: cannot lookup tree");
        goto cleanup;
    }

    err = git_signature_now(&sig, author_name, author_email);
    if (err < 0) {
        _n_git_log_error("n_git_commit");
        _n_git_set_status(repo, "commit failed: cannot create signature");
        goto cleanup;
    }

    /* try to get HEAD as parent; if no HEAD this is the initial commit */
    int have_parent = 0;
    err = git_repository_head(&head_ref, repo->repo);
    if (err == 0) {
        err = git_reference_peel((git_object**)&parent_commit, head_ref, GIT_OBJECT_COMMIT);
        if (err == 0) {
            have_parent = 1;
        }
    }

    if (have_parent) {
        const git_commit* parents[] = {parent_commit};
        err = git_commit_create(&commit_oid, repo->repo, "HEAD", sig, sig, "UTF-8", message, tree, 1, parents);
    } else {
        err = git_commit_create(&commit_oid, repo->repo, "HEAD", sig, sig, "UTF-8", message, tree, 0, NULL);
    }

    if (err < 0) {
        _n_git_log_error("n_git_commit");
        _n_git_set_status(repo, "commit failed");
        goto cleanup;
    }

    {
        char hex[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(hex, sizeof(hex), &commit_oid);
        _n_git_set_status(repo, "committed %.8s: %s", hex, message);
    }
    ret = 0;

cleanup:
    if (parent_commit) git_commit_free(parent_commit);
    if (head_ref) git_reference_free(head_ref);
    if (sig) git_signature_free(sig);
    if (tree) git_tree_free(tree);
    git_index_free(index);
    return ret;
} /* n_git_commit */

/**
 *@brief Retrieve commit log entries starting from HEAD.
 *@param repo An opened N_GIT_REPO handle.
 *@param max_entries Maximum number of log entries to retrieve.
 *@return A LIST of N_GIT_COMMIT_INFO pointers, or NULL on failure.
 */
LIST* n_git_log(N_GIT_REPO* repo, size_t max_entries) {
    __n_assert(repo, return NULL);
    __n_assert(repo->repo, return NULL);

    git_revwalk* walker = NULL;
    int err = git_revwalk_new(&walker, repo->repo);
    if (err < 0) {
        _n_git_log_error("n_git_log");
        return NULL;
    }

    git_revwalk_sorting(walker, GIT_SORT_TIME);

    err = git_revwalk_push_head(walker);
    if (err < 0) {
        /* Unborn HEAD (empty repo, no commits), return an empty list.
         * git_repository_head_unborn() returns:
         *   1  => unborn HEAD
         *   0  => not unborn
         *  <0  => error
         */
        git_revwalk_free(walker);
        int unborn = git_repository_head_unborn(repo->repo);
        if (unborn == 1) {
            return new_generic_list(UNLIMITED_LIST_ITEMS);
        }
        if (unborn < 0) {
            _n_git_log_error("n_git_log");
        }
        return NULL;
    }

    LIST* list = new_generic_list(UNLIMITED_LIST_ITEMS);
    if (!list) {
        git_revwalk_free(walker);
        return NULL;
    }

    git_oid oid;
    size_t count = 0;
    while (count < max_entries && git_revwalk_next(&oid, walker) == 0) {
        git_commit* commit = NULL;
        err = git_commit_lookup(&commit, repo->repo, &oid);
        if (err < 0) {
            _n_git_log_error("n_git_log");
            continue;
        }

        N_GIT_COMMIT_INFO* info = NULL;
        Malloc(info, N_GIT_COMMIT_INFO, 1);
        if (!info) {
            git_commit_free(commit);
            continue;
        }

        /* hash */
        char hex[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(hex, sizeof(hex), &oid);
        snprintf(info->hash, sizeof(info->hash), "%s", hex);
        snprintf(info->short_hash, sizeof(info->short_hash), "%.8s", hex);

        /* message */
        const char* msg = git_commit_message(commit);
        if (msg) {
            snprintf(info->message, sizeof(info->message), "%s", msg);
        }

        /* author */
        const git_signature* author = git_commit_author(commit);
        if (author) {
            snprintf(info->author, sizeof(info->author), "%s <%s>", author->name ? author->name : "", author->email ? author->email : "");
            info->timestamp = (int64_t)author->when.time;
        }

        list_push(list, info, _n_git_commit_info_destroy);
        git_commit_free(commit);
        count++;
    }

    git_revwalk_free(walker);
    return list;
} /* n_git_log */

/**
 *@brief Get a diff of the working directory against the index.
 *@param repo An opened N_GIT_REPO handle.
 *@return An N_STR containing the diff text, or NULL on failure.
 */
N_STR* n_git_diff_workdir(N_GIT_REPO* repo) {
    __n_assert(repo, return NULL);
    __n_assert(repo->repo, return NULL);

    git_diff* diff = NULL;
    int err = git_diff_index_to_workdir(&diff, repo->repo, NULL, NULL);
    if (err < 0) {
        _n_git_log_error("n_git_diff_workdir");
        return NULL;
    }

    N_STR* result = new_nstr(256);
    if (!result) {
        git_diff_free(diff);
        return NULL;
    }

    git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, _n_git_diff_print_cb, &result);

    git_diff_free(diff);
    return result;
} /* n_git_diff_workdir */

/**
 *@brief Get a diff between two commits identified by hash.
 *@param repo An opened N_GIT_REPO handle.
 *@param hash_a Hash (or prefix) of the first commit.
 *@param hash_b Hash (or prefix) of the second commit.
 *@return An N_STR containing the diff text, or NULL on failure.
 */
N_STR* n_git_diff_commits(N_GIT_REPO* repo, const char* hash_a, const char* hash_b) {
    __n_assert(repo, return NULL);
    __n_assert(repo->repo, return NULL);
    __n_assert(hash_a, return NULL);
    __n_assert(hash_b, return NULL);

    git_object* obj_a = NULL;
    git_object* obj_b = NULL;
    git_commit* commit_a = NULL;
    git_commit* commit_b = NULL;
    git_tree* tree_a = NULL;
    git_tree* tree_b = NULL;
    git_diff* diff = NULL;
    N_STR* result = NULL;
    int err = 0;

    err = git_revparse_single(&obj_a, repo->repo, hash_a);
    if (err < 0) {
        _n_git_log_error("n_git_diff_commits");
        goto cleanup;
    }

    err = git_revparse_single(&obj_b, repo->repo, hash_b);
    if (err < 0) {
        _n_git_log_error("n_git_diff_commits");
        goto cleanup;
    }

    err = git_commit_lookup(&commit_a, repo->repo, git_object_id(obj_a));
    if (err < 0) {
        _n_git_log_error("n_git_diff_commits");
        goto cleanup;
    }

    err = git_commit_lookup(&commit_b, repo->repo, git_object_id(obj_b));
    if (err < 0) {
        _n_git_log_error("n_git_diff_commits");
        goto cleanup;
    }

    err = git_commit_tree(&tree_a, commit_a);
    if (err < 0) {
        _n_git_log_error("n_git_diff_commits");
        goto cleanup;
    }

    err = git_commit_tree(&tree_b, commit_b);
    if (err < 0) {
        _n_git_log_error("n_git_diff_commits");
        goto cleanup;
    }

    err = git_diff_tree_to_tree(&diff, repo->repo, tree_a, tree_b, NULL);
    if (err < 0) {
        _n_git_log_error("n_git_diff_commits");
        goto cleanup;
    }

    result = new_nstr(256);
    if (!result) {
        goto cleanup;
    }

    git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, _n_git_diff_print_cb, &result);

cleanup:
    if (diff) git_diff_free(diff);
    if (tree_b) git_tree_free(tree_b);
    if (tree_a) git_tree_free(tree_a);
    if (commit_b) git_commit_free(commit_b);
    if (commit_a) git_commit_free(commit_a);
    if (obj_b) git_object_free(obj_b);
    if (obj_a) git_object_free(obj_a);
    return result;
} /* n_git_diff_commits */

/**
 *@brief Restore a single file from HEAD (discard working directory changes).
 *@param repo An opened N_GIT_REPO handle.
 *@param filepath Relative path of the file to restore.
 *@return 0 on success, -1 on failure.
 */
int n_git_checkout_path(N_GIT_REPO* repo, const char* filepath) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(filepath, return -1);

    char* path_str = (char*)filepath;
    git_strarray paths;
    paths.strings = &path_str;
    paths.count = 1;

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;
    opts.paths = paths;

    int err = git_checkout_head(repo->repo, &opts);
    if (err < 0) {
        _n_git_log_error("n_git_checkout_path");
        _n_git_set_status(repo, "restore failed: %s", filepath);
        return -1;
    }

    _n_git_set_status(repo, "restored: %s", filepath);
    return 0;
} /* n_git_checkout_path */

/**
 *@brief Checkout a specific commit (detached HEAD).
 *@param repo An opened N_GIT_REPO handle.
 *@param hash Hash (or prefix) of the commit to checkout.
 *@return 0 on success, -1 on failure.
 */
int n_git_checkout_commit(N_GIT_REPO* repo, const char* hash) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(hash, return -1);

    git_object* obj = NULL;
    int ret = -1;

    int err = git_revparse_single(&obj, repo->repo, hash);
    if (err < 0) {
        _n_git_log_error("n_git_checkout_commit");
        return -1;
    }

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;

    err = git_checkout_tree(repo->repo, obj, &opts);
    if (err < 0) {
        _n_git_log_error("n_git_checkout_commit");
        goto cleanup;
    }

    err = git_repository_set_head_detached(repo->repo, git_object_id(obj));
    if (err < 0) {
        _n_git_log_error("n_git_checkout_commit");
        goto cleanup;
    }

    ret = 0;

cleanup:
    git_object_free(obj);
    return ret;
} /* n_git_checkout_commit */

/**
 *@brief List all local branch names.
 *@param repo An opened N_GIT_REPO handle.
 *@return A LIST of char* branch name strings, or NULL on failure.
 */
LIST* n_git_list_branches(N_GIT_REPO* repo) {
    __n_assert(repo, return NULL);
    __n_assert(repo->repo, return NULL);

    git_branch_iterator* iter = NULL;
    int err = git_branch_iterator_new(&iter, repo->repo, GIT_BRANCH_LOCAL);
    if (err < 0) {
        _n_git_log_error("n_git_list_branches");
        return NULL;
    }

    LIST* list = new_generic_list(UNLIMITED_LIST_ITEMS);
    if (!list) {
        git_branch_iterator_free(iter);
        return NULL;
    }

    git_reference* ref = NULL;
    git_branch_t type;
    while (git_branch_next(&ref, &type, iter) == 0) {
        const char* name = NULL;
        err = git_branch_name(&name, ref);
        if (err == 0 && name) {
            char* dup = strdup(name);
            if (dup) {
                list_push(list, dup, _n_git_string_destroy);
            }
        }
        git_reference_free(ref);
    }

    git_branch_iterator_free(iter);
    return list;
} /* n_git_list_branches */

/**
 *@brief Create a new branch from HEAD.
 * On an empty repository (no commits yet), this updates HEAD to point
 * to the new branch symbolically (equivalent to git checkout -b).
 *@param repo An opened N_GIT_REPO handle.
 *@param branch_name Name for the new branch.
 *@return 0 on success, -1 on failure.
 */
int n_git_create_branch(N_GIT_REPO* repo, const char* branch_name) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(branch_name, return -1);

    git_reference* head_ref = NULL;
    git_commit* head_commit = NULL;
    git_reference* new_branch = NULL;
    int ret = -1;

    int err = git_repository_head(&head_ref, repo->repo);
    if (err == GIT_EUNBORNBRANCH) {
        /* Empty repository (no commits yet).  We cannot create a real branch
         * ref because there is no commit to point to.  Instead, update the
         * symbolic HEAD to target the requested branch, this is equivalent
         * to `git checkout -b <branch>` on an empty repo. */
        char refname[512];
        int n = snprintf(refname, sizeof(refname), "refs/heads/%s", branch_name);
        if (n < 0 || (size_t)n >= sizeof(refname)) {
            n_log(LOG_ERR, "n_git_create_branch: branch name too long");
            return -1;
        }
        git_reference* new_ref = NULL;
        int sym_err = git_reference_symbolic_create(&new_ref, repo->repo, "HEAD", refname, 1, "n_git_create_branch: set HEAD on empty repo");
        if (sym_err < 0) {
            _n_git_log_error("n_git_create_branch (unborn HEAD)");
            return -1;
        }
        git_reference_free(new_ref);
        n_log(LOG_INFO, "n_git_create_branch: set HEAD to '%s' on empty repository", branch_name);
        _n_git_set_status(repo, "created branch '%s' (empty repo)", branch_name);
        return 0;
    }
    if (err < 0) {
        _n_git_log_error("n_git_create_branch");
        _n_git_set_status(repo, "create branch '%s' failed", branch_name);
        return -1;
    }

    err = git_reference_peel((git_object**)&head_commit, head_ref, GIT_OBJECT_COMMIT);
    if (err < 0) {
        _n_git_log_error("n_git_create_branch");
        _n_git_set_status(repo, "create branch '%s' failed", branch_name);
        goto cleanup;
    }

    err = git_branch_create(&new_branch, repo->repo, branch_name, head_commit, 0);
    if (err < 0) {
        _n_git_log_error("n_git_create_branch");
        _n_git_set_status(repo, "create branch '%s' failed", branch_name);
        goto cleanup;
    }

    _n_git_set_status(repo, "created branch '%s'", branch_name);
    ret = 0;

cleanup:
    if (new_branch) git_reference_free(new_branch);
    if (head_commit) git_commit_free(head_commit);
    git_reference_free(head_ref);
    return ret;
} /* n_git_create_branch */

/**
 *@brief Switch to an existing local branch.
 *@param repo An opened N_GIT_REPO handle.
 *@param branch_name Name of the branch to switch to.
 *@return 0 on success, -1 on failure.
 */
int n_git_switch_branch(N_GIT_REPO* repo, const char* branch_name) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(branch_name, return -1);

    git_reference* branch_ref = NULL;
    int ret = -1;

    int err = git_branch_lookup(&branch_ref, repo->repo, branch_name, GIT_BRANCH_LOCAL);
    if (err < 0) {
        _n_git_log_error("n_git_switch_branch");
        _n_git_set_status(repo, "switch to '%s' failed: branch not found", branch_name);
        return -1;
    }

    char refname[512];
    snprintf(refname, sizeof(refname), "refs/heads/%s", branch_name);

    err = git_repository_set_head(repo->repo, refname);
    if (err < 0) {
        _n_git_log_error("n_git_switch_branch");
        _n_git_set_status(repo, "switch to '%s' failed", branch_name);
        goto cleanup;
    }

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;

    err = git_checkout_head(repo->repo, &opts);
    if (err < 0) {
        _n_git_log_error("n_git_switch_branch");
        _n_git_set_status(repo, "switch to '%s' failed", branch_name);
        goto cleanup;
    }

    _n_git_set_status(repo, "switched to branch '%s'", branch_name);
    ret = 0;

cleanup:
    git_reference_free(branch_ref);
    return ret;
} /* n_git_switch_branch */

/**
 *@brief Get the name of the current branch.
 *@param repo An opened N_GIT_REPO handle.
 *@param out Buffer to receive the branch name.
 *@param out_size Size of the output buffer.
 *@return 0 on success, -1 on failure.
 */
int n_git_current_branch(N_GIT_REPO* repo, char* out, size_t out_size) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(out, return -1);
    __n_assert(out_size > 0, return -1);

    git_reference* head_ref = NULL;

    int err = git_repository_head(&head_ref, repo->repo);
    if (err == 0) {
        const char* shorthand = git_reference_shorthand(head_ref);
        snprintf(out, out_size, "%s", shorthand ? shorthand : "");
        git_reference_free(head_ref);
        return 0;
    }

    /* Handle unborn HEAD (empty repo, no commits yet).
     * Read the symbolic HEAD reference to determine the branch name. */
    if (err == GIT_EUNBORNBRANCH) {
        git_reference* sym_ref = NULL;
        int sym_err = git_reference_lookup(&sym_ref, repo->repo, "HEAD");
        if (sym_err == 0 && git_reference_type(sym_ref) == GIT_REFERENCE_SYMBOLIC) {
            const char* target = git_reference_symbolic_target(sym_ref);
            /* target is e.g. "refs/heads/main", extract branch name */
            if (target) {
                const char* prefix = "refs/heads/";
                size_t prefix_len = strlen(prefix);
                if (strncmp(target, prefix, prefix_len) == 0) {
                    snprintf(out, out_size, "%s", target + prefix_len);
                } else {
                    snprintf(out, out_size, "%s", target);
                }
                git_reference_free(sym_ref);
                return 0;
            }
        }
        if (sym_ref) git_reference_free(sym_ref);
    }

    _n_git_log_error("n_git_current_branch");
    return -1;
} /* n_git_current_branch */

/**
 *@brief Delete a local branch.
 *@param repo An opened N_GIT_REPO handle.
 *@param branch_name Name of the branch to delete.
 *@return 0 on success, -1 on failure.
 */
int n_git_delete_branch(N_GIT_REPO* repo, const char* branch_name) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(branch_name, return -1);

    git_reference* branch_ref = NULL;

    int err = git_branch_lookup(&branch_ref, repo->repo, branch_name, GIT_BRANCH_LOCAL);
    if (err < 0) {
        _n_git_log_error("n_git_delete_branch");
        return -1;
    }

    err = git_branch_delete(branch_ref);
    git_reference_free(branch_ref);

    if (err < 0) {
        _n_git_log_error("n_git_delete_branch");
        return -1;
    }

    return 0;
} /* n_git_delete_branch */

/* REMOTE OPERATIONS: auth, push, pull */

/**
 *@brief libgit2 credential callback for remote operations.
 */
static int _n_git_cred_cb(git_credential** out, const char* url, const char* username_from_url, unsigned int allowed_types, void* payload) {
    (void)url;
    N_GIT_REMOTE_AUTH* auth = (N_GIT_REMOTE_AUTH*)payload;
    if (!auth) return GIT_PASSTHROUGH;

    switch (auth->type) {
        case N_GIT_AUTH_TOKEN:
            if ((allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) &&
                auth->password) {
                const char* user = auth->username ? auth->username : "x-access-token";
                return git_credential_userpass_plaintext_new(out, user, auth->password);
            }
            break;

        case N_GIT_AUTH_BASIC:
            if ((allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) &&
                auth->username && auth->password) {
                return git_credential_userpass_plaintext_new(out, auth->username, auth->password);
            }
            break;

        case N_GIT_AUTH_SSH:
            if ((allowed_types & GIT_CREDENTIAL_SSH_KEY) &&
                auth->ssh_privkey_path) {
                const char* user = auth->username ? auth->username : (username_from_url ? username_from_url : "git");
                return git_credential_ssh_key_new(out, user,
                                                  auth->ssh_pubkey_path, auth->ssh_privkey_path,
                                                  auth->ssh_passphrase ? auth->ssh_passphrase : "");
            }
            /* Try SSH agent if key auth not available */
            if (allowed_types & GIT_CREDENTIAL_SSH_KEY) {
                const char* user = auth->username ? auth->username : (username_from_url ? username_from_url : "git");
                return git_credential_ssh_key_from_agent(out, user);
            }
            break;

        default:
            break;
    }

    return GIT_PASSTHROUGH;
} /* _n_git_cred_cb */

/**
 *@brief Set remote authentication credentials on a repo handle.
 */
int n_git_set_remote_auth(N_GIT_REPO* repo, const N_GIT_REMOTE_AUTH* auth) {
    __n_assert(repo, return -1);
    __n_assert(auth, return -1);

    /* Free existing auth */
    if (repo->remote_auth) {
        FreeNoLog(repo->remote_auth->username);
        FreeNoLog(repo->remote_auth->password);
        FreeNoLog(repo->remote_auth->ssh_pubkey_path);
        FreeNoLog(repo->remote_auth->ssh_privkey_path);
        FreeNoLog(repo->remote_auth->ssh_passphrase);
        FreeNoLog(repo->remote_auth);
    }

    N_GIT_REMOTE_AUTH* a = NULL;
    Malloc(a, N_GIT_REMOTE_AUTH, 1);
    __n_assert(a, return -1);
    memset(a, 0, sizeof(*a));

    a->type = auth->type;
    if (auth->username) a->username = strdup(auth->username);
    if (auth->password) a->password = strdup(auth->password);
    if (auth->ssh_pubkey_path) a->ssh_pubkey_path = strdup(auth->ssh_pubkey_path);
    if (auth->ssh_privkey_path) a->ssh_privkey_path = strdup(auth->ssh_privkey_path);
    if (auth->ssh_passphrase) a->ssh_passphrase = strdup(auth->ssh_passphrase);

    repo->remote_auth = a;
    return 0;
} /* n_git_set_remote_auth */

/**
 *@brief Ensure a remote named "origin" exists with the given URL.
 */
int n_git_remote_set_url(N_GIT_REPO* repo, const char* url) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);
    __n_assert(url, return -1);

    _n_git_ensure_init();

    git_remote* remote = NULL;
    int err = git_remote_lookup(&remote, repo->repo, "origin");
    if (err == 0) {
        /* Remote exists, update URL if different */
        const char* current_url = git_remote_url(remote);
        if (!current_url || strcmp(current_url, url) != 0) {
            git_remote_free(remote);
            err = git_remote_set_url(repo->repo, "origin", url);
            if (err < 0) {
                _n_git_log_error("n_git_remote_set_url: set_url");
                return -1;
            }
        } else {
            git_remote_free(remote);
        }
    } else {
        /* Remote does not exist, create it */
        err = git_remote_create(&remote, repo->repo, "origin", url);
        if (err < 0) {
            _n_git_log_error("n_git_remote_set_url: create");
            return -1;
        }
        git_remote_free(remote);
    }

    return 0;
} /* n_git_remote_set_url */

/**
 *@brief Push the current branch to the "origin" remote.
 */
int n_git_push(N_GIT_REPO* repo) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);

    _n_git_ensure_init();

    /* Refuse to push an empty repo (no commits yet).
     * git_repository_head_unborn() returns 1 if unborn, 0 if not, <0 on error. */
    int unborn = git_repository_head_unborn(repo->repo);
    if (unborn < 0) {
        _n_git_log_error("n_git_push: head_unborn");
        _n_git_set_status(repo, "push failed: unable to check HEAD");
        return -1;
    }
    if (unborn == 1) {
        n_log(LOG_ERR, "n_git_push: repository has no commits, commit first before pushing");
        _n_git_set_status(repo, "push failed: no commits yet, commit first");
        return -1;
    }

    /* Get current branch name */
    char branch[128];
    if (n_git_current_branch(repo, branch, sizeof(branch)) != 0) {
        n_log(LOG_ERR, "n_git_push: cannot determine current branch");
        _n_git_set_status(repo, "push failed: cannot determine current branch");
        return -1;
    }

    /* Build refspec: refs/heads/branch:refs/heads/branch */
    char refspec[300];
    snprintf(refspec, sizeof(refspec), "refs/heads/%s:refs/heads/%s",
             branch, branch);

    git_remote* remote = NULL;
    int err = git_remote_lookup(&remote, repo->repo, "origin");
    if (err < 0) {
        _n_git_log_error("n_git_push: remote lookup");
        _n_git_set_status(repo, "push failed: remote 'origin' not found");
        return -1;
    }

    /* Set up push options with credential callback */
    git_push_options opts = GIT_PUSH_OPTIONS_INIT;
    if (repo->remote_auth) {
        opts.callbacks.credentials = _n_git_cred_cb;
        opts.callbacks.payload = repo->remote_auth;
    }

    const char* refspecs[] = {refspec};
    git_strarray refspec_arr = {(char**)refspecs, 1};

    err = git_remote_push(remote, &refspec_arr, &opts);
    git_remote_free(remote);

    if (err < 0) {
        const git_error* gerr = git_error_last();
        _n_git_log_error("n_git_push");
        _n_git_set_status(repo, "push failed: %s",
                          (gerr && gerr->message) ? gerr->message : "unknown error");
        return -1;
    }

    n_log(LOG_INFO, "n_git_push: pushed %s to origin", branch);
    _n_git_set_status(repo, "pushed '%s' to origin", branch);
    return 0;
} /* n_git_push */

/**
 *@brief Fetch from "origin" and fast-forward merge the current branch.
 */
int n_git_pull(N_GIT_REPO* repo) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);

    _n_git_ensure_init();

    /* Fetch from origin */
    git_remote* remote = NULL;
    int err = git_remote_lookup(&remote, repo->repo, "origin");
    if (err < 0) {
        _n_git_log_error("n_git_pull: remote lookup");
        _n_git_set_status(repo, "pull failed: remote 'origin' not found");
        return -1;
    }

    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    if (repo->remote_auth) {
        fetch_opts.callbacks.credentials = _n_git_cred_cb;
        fetch_opts.callbacks.payload = repo->remote_auth;
    }

    err = git_remote_fetch(remote, NULL, &fetch_opts, "pull");
    git_remote_free(remote);

    if (err < 0) {
        const git_error* gerr = git_error_last();
        _n_git_log_error("n_git_pull: fetch");
        _n_git_set_status(repo, "pull failed: fetch error: %s",
                          (gerr && gerr->message) ? gerr->message : "unknown error");
        return -1;
    }

    /* Get current branch name */
    char branch[128];
    if (n_git_current_branch(repo, branch, sizeof(branch)) != 0) {
        n_log(LOG_ERR, "n_git_pull: cannot determine current branch");
        _n_git_set_status(repo, "pull failed: cannot determine current branch");
        return -1;
    }

    /* Look up the remote tracking ref: refs/remotes/origin/<branch> */
    char remote_ref[300];
    snprintf(remote_ref, sizeof(remote_ref), "refs/remotes/origin/%s", branch);

    git_oid remote_oid;
    err = git_reference_name_to_id(&remote_oid, repo->repo, remote_ref);
    if (err < 0) {
        _n_git_log_error("n_git_pull: remote ref lookup");
        _n_git_set_status(repo, "pull failed: no remote tracking branch for '%s'", branch);
        return -1;
    }

    /* Get HEAD commit */
    git_oid local_oid;
    err = git_reference_name_to_id(&local_oid, repo->repo, "HEAD");
    if (err < 0) {
        _n_git_log_error("n_git_pull: HEAD lookup");
        _n_git_set_status(repo, "pull failed: cannot read HEAD");
        return -1;
    }

    /* Check if already up to date */
    if (git_oid_equal(&local_oid, &remote_oid)) {
        n_log(LOG_INFO, "n_git_pull: already up to date");
        _n_git_set_status(repo, "already up to date");
        return 0;
    }

    /* Check if fast-forward is possible */
    git_annotated_commit* remote_commit = NULL;
    err = git_annotated_commit_lookup(&remote_commit, repo->repo, &remote_oid);
    if (err < 0) {
        _n_git_log_error("n_git_pull: annotated commit lookup");
        _n_git_set_status(repo, "pull failed: cannot lookup remote commit");
        return -1;
    }

    git_merge_analysis_t analysis;
    git_merge_preference_t preference;
    const git_annotated_commit* their_heads[] = {remote_commit};
    err = git_merge_analysis(&analysis, &preference, repo->repo, their_heads, 1);
    if (err < 0) {
        git_annotated_commit_free(remote_commit);
        _n_git_log_error("n_git_pull: merge analysis");
        _n_git_set_status(repo, "pull failed: merge analysis error");
        return -1;
    }

    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
        git_annotated_commit_free(remote_commit);
        n_log(LOG_INFO, "n_git_pull: already up to date");
        _n_git_set_status(repo, "already up to date");
        return 0;
    }

    if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) {
        /* Fast-forward: move HEAD to remote commit */
        git_reference* head_ref = NULL;
        err = git_repository_head(&head_ref, repo->repo);
        if (err < 0) {
            git_annotated_commit_free(remote_commit);
            _n_git_log_error("n_git_pull: get HEAD ref");
            _n_git_set_status(repo, "pull failed: cannot get HEAD reference");
            return -1;
        }

        git_reference* new_ref = NULL;
        err = git_reference_set_target(&new_ref, head_ref, &remote_oid, "pull: fast-forward");
        git_reference_free(head_ref);
        if (err < 0) {
            git_annotated_commit_free(remote_commit);
            _n_git_log_error("n_git_pull: set target");
            _n_git_set_status(repo, "pull failed: cannot fast-forward HEAD");
            return -1;
        }
        git_reference_free(new_ref);

        /* Checkout the new HEAD */
        git_object* target = NULL;
        git_object_lookup(&target, repo->repo, &remote_oid, GIT_OBJECT_COMMIT);
        if (target) {
            git_checkout_options co_opts = GIT_CHECKOUT_OPTIONS_INIT;
            co_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
            git_checkout_tree(repo->repo, target, &co_opts);
            git_object_free(target);
        }

        git_annotated_commit_free(remote_commit);
        n_log(LOG_INFO, "n_git_pull: fast-forwarded %s", branch);
        _n_git_set_status(repo, "pulled '%s': fast-forwarded to latest", branch);
        return 0;
    }

    /* Not a fast-forward, we don't handle merge conflicts */
    git_annotated_commit_free(remote_commit);
    n_log(LOG_ERR, "n_git_pull: cannot fast-forward, manual merge required");
    _n_git_set_status(repo, "pull failed: cannot fast-forward, manual merge required");
    return -1;
} /* n_git_pull */

/**
 *@brief Fetch from the "origin" remote without merging.
 */
int n_git_fetch(N_GIT_REPO* repo) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);

    _n_git_ensure_init();

    git_remote* remote = NULL;
    int err = git_remote_lookup(&remote, repo->repo, "origin");
    if (err < 0) {
        _n_git_log_error("n_git_fetch: remote lookup");
        _n_git_set_status(repo, "fetch failed: remote 'origin' not found");
        return -1;
    }

    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    if (repo->remote_auth) {
        fetch_opts.callbacks.credentials = _n_git_cred_cb;
        fetch_opts.callbacks.payload = repo->remote_auth;
    }

    err = git_remote_fetch(remote, NULL, &fetch_opts, "fetch");
    git_remote_free(remote);

    if (err < 0) {
        const git_error* gerr = git_error_last();
        _n_git_log_error("n_git_fetch");
        _n_git_set_status(repo, "fetch failed: %s",
                          (gerr && gerr->message) ? gerr->message : "unknown error");
        return -1;
    }

    n_log(LOG_INFO, "n_git_fetch: fetched from origin");
    _n_git_set_status(repo, "fetched from origin");
    return 0;
} /* n_git_fetch */

/**
 *@brief Count commits ahead and behind relative to the remote tracking branch.
 */
int n_git_ahead_behind(N_GIT_REPO* repo, size_t* ahead, size_t* behind) {
    __n_assert(repo, return -1);
    __n_assert(repo->repo, return -1);

    _n_git_ensure_init();

    if (ahead) *ahead = 0;
    if (behind) *behind = 0;

    /* Check for unborn HEAD (no commits yet) */
    int unborn = git_repository_head_unborn(repo->repo);
    if (unborn < 0) {
        _n_git_log_error("n_git_ahead_behind: head_unborn");
        _n_git_set_status(repo, "ahead/behind: unable to check HEAD");
        return -1;
    }
    if (unborn == 1) {
        /* No commits yet, nothing to compare */
        _n_git_set_status(repo, "no commits yet");
        return 0;
    }

    /* Get current branch name, 256 bytes covers practical branch names */
    char branch[256];
    if (n_git_current_branch(repo, branch, sizeof(branch)) != 0) {
        _n_git_set_status(repo, "ahead/behind: cannot determine current branch");
        return -1;
    }

    /* Resolve local HEAD */
    git_oid local_oid;
    int err = git_reference_name_to_id(&local_oid, repo->repo, "HEAD");
    if (err < 0) {
        _n_git_log_error("n_git_ahead_behind: HEAD lookup");
        _n_git_set_status(repo, "ahead/behind: cannot read HEAD");
        return -1;
    }

    /* Resolve remote tracking ref */
    char remote_ref[512];
    int ref_len = snprintf(remote_ref, sizeof(remote_ref),
                           "refs/remotes/origin/%s", branch);
    if (ref_len < 0 || (size_t)ref_len >= sizeof(remote_ref)) {
        _n_git_set_status(repo, "ahead/behind: branch name too long");
        return -1;
    }
    git_oid remote_oid;
    err = git_reference_name_to_id(&remote_oid, repo->repo, remote_ref);
    if (err < 0) {
        /* Distinguish "no tracking branch" from other libgit2 errors */
        if (err == GIT_ENOTFOUND || err == GIT_EINVALIDSPEC) {
            _n_git_set_status(repo, "ahead/behind: no remote tracking branch for '%s'", branch);
        } else {
            const git_error* e;
            _n_git_log_error("n_git_ahead_behind: remote tracking ref lookup");
            e = git_error_last();
            if (e && e->message) {
                _n_git_set_status(repo, "ahead/behind: %s", e->message);
            } else {
                _n_git_set_status(repo, "ahead/behind: failed to read remote tracking branch");
            }
        }
        return -1;
    }

    /* Already equal? */
    if (git_oid_equal(&local_oid, &remote_oid)) {
        _n_git_set_status(repo, "up to date with origin/%s", branch);
        return 0;
    }

    /* Compute ahead/behind counts */
    size_t a = 0, b = 0;
    err = git_graph_ahead_behind(&a, &b, repo->repo, &local_oid, &remote_oid);
    if (err < 0) {
        _n_git_log_error("n_git_ahead_behind: graph_ahead_behind");
        _n_git_set_status(repo, "ahead/behind: comparison failed");
        return -1;
    }

    if (ahead) *ahead = a;
    if (behind) *behind = b;

    _n_git_set_status(repo, "origin/%s: %zu ahead, %zu behind", branch, a, b);
    return 0;
} /* n_git_ahead_behind */

#pragma GCC diagnostic pop

#endif /* HAVE_LIBGIT2 */
