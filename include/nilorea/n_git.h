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
 *@file n_git.h
 *@brief libgit2 wrapper for Git repository operations
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/03/2026
 */

#ifndef __N_GIT_H__
#define __N_GIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_LIBGIT2

/**@defgroup GIT GIT: libgit2 wrapper for repository operations
  @addtogroup GIT
  @{
  */

#include "nilorea/n_str.h"
#include "nilorea/n_list.h"
#include <git2.h>

/*! File is new / untracked */
#define N_GIT_STATUS_NEW 1
/*! File has been modified */
#define N_GIT_STATUS_MODIFIED 2
/*! File has been deleted */
#define N_GIT_STATUS_DELETED 4
/*! File is staged in the index */
#define N_GIT_STATUS_STAGED 8

/*! Remote auth: no authentication */
#define N_GIT_AUTH_NONE 0
/*! Remote auth: personal access token (used as password with empty or "x-access-token" user) */
#define N_GIT_AUTH_TOKEN 1
/*! Remote auth: username + password */
#define N_GIT_AUTH_BASIC 2
/*! Remote auth: SSH key file */
#define N_GIT_AUTH_SSH 3

/*! Credentials for remote operations */
typedef struct N_GIT_REMOTE_AUTH {
    /*! auth type: N_GIT_AUTH_NONE/TOKEN/BASIC/SSH */
    int type;
    /*! username for basic auth or SSH, or NULL */
    char* username;
    /*! password or personal access token, or NULL */
    char* password;
    /*! path to SSH public key file, or NULL (derived from private key + ".pub") */
    char* ssh_pubkey_path;
    /*! path to SSH private key file, or NULL */
    char* ssh_privkey_path;
    /*! passphrase for SSH key, or NULL */
    char* ssh_passphrase;
} N_GIT_REMOTE_AUTH;

/*! Wrapper around a git_repository handle */
typedef struct N_GIT_REPO {
    /*! libgit2 repository handle */
    git_repository* repo;
    /*! filesystem path to the repository */
    char* path;
    /*! credentials for remote operations, or NULL */
    N_GIT_REMOTE_AUTH* remote_auth;
    /*! human-readable status message from the last operation */
    char last_status[512];
} N_GIT_REPO;

/*! Single file status entry */
typedef struct N_GIT_STATUS_ENTRY {
    /*! relative path of the file */
    char path[512];
    /*! combination of N_GIT_STATUS_* flags */
    int flags;
} N_GIT_STATUS_ENTRY;

/*! Commit metadata */
typedef struct N_GIT_COMMIT_INFO {
    /*! full hex hash */
    char hash[42];
    /*! short (abbreviated) hex hash */
    char short_hash[10];
    /*! first line of commit message */
    char message[512];
    /*! author name and email */
    char author[256];
    /*! unix timestamp of the commit */
    int64_t timestamp;
} N_GIT_COMMIT_INFO;

/*! @brief Open an existing Git repository.
 *  @param path Filesystem path to the repository root (or .git dir).
 *  @return A new N_GIT_REPO handle, or NULL on failure.
 */
N_GIT_REPO* n_git_open(const char* path);

/*! @brief Initialize a new Git repository.
 *  @param path Filesystem path where the repository will be created.
 *  @return A new N_GIT_REPO handle, or NULL on failure.
 */
N_GIT_REPO* n_git_init(const char* path);

/*! @brief Initialize a new Git repository with a custom initial branch name.
 *  @param path Filesystem path where the repository will be created.
 *  @param initial_branch Name of the initial branch (e.g. "main"). If NULL, uses libgit2 default.
 *  @return A new N_GIT_REPO handle, or NULL on failure.
 */
N_GIT_REPO* n_git_init_ex(const char* path, const char* initial_branch);

/*! @brief Close a Git repository and free resources.
 *  @param repo Pointer to the N_GIT_REPO handle (set to NULL after close).
 */
void n_git_close(N_GIT_REPO** repo);

/*! @brief Get the status of all files in the working directory.
 *  @param repo An opened N_GIT_REPO handle.
 *  @return A LIST of N_GIT_STATUS_ENTRY pointers, or NULL on failure.
 */
LIST* n_git_status(N_GIT_REPO* repo);

/*! @brief Stage a file or directory by path.
 *
 *  If filepath ends with '/' it is treated as a directory and all files
 *  beneath it are staged via pathspec matching (git_index_add_all).
 *  Otherwise a single file is staged via git_index_add_bypath.
 *
 *  Paths should use forward slashes as separators (the convention used
 *  by libgit2 on all platforms).
 *
 *  @param repo An opened N_GIT_REPO handle.
 *  @param filepath Relative path of the file or directory to stage.
 *  @return 0 on success, -1 on failure.
 */
int n_git_stage(N_GIT_REPO* repo, const char* filepath);

/*! @brief Stage all modified and untracked files.
 *  @param repo An opened N_GIT_REPO handle.
 *  @return 0 on success, -1 on failure.
 */
int n_git_stage_all(N_GIT_REPO* repo);

/*! @brief Unstage a single file (reset from HEAD).
 *  @param repo An opened N_GIT_REPO handle.
 *  @param filepath Relative path of the file to unstage.
 *  @return 0 on success, -1 on failure.
 */
int n_git_unstage(N_GIT_REPO* repo, const char* filepath);

/*! @brief Create a commit from the current index.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param message The commit message.
 *  @param author_name Author name string.
 *  @param author_email Author email string.
 *  @return 0 on success, -1 on failure.
 */
int n_git_commit(N_GIT_REPO* repo, const char* message, const char* author_name, const char* author_email);

/*! @brief Retrieve commit log entries starting from HEAD.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param max_entries Maximum number of log entries to retrieve.
 *  @return A LIST of N_GIT_COMMIT_INFO pointers, or NULL on failure.
 */
LIST* n_git_log(N_GIT_REPO* repo, size_t max_entries);

/*! @brief Get a diff of the working directory against the index.
 *  @param repo An opened N_GIT_REPO handle.
 *  @return An N_STR containing the diff text, or NULL on failure.
 */
N_STR* n_git_diff_workdir(N_GIT_REPO* repo);

/*! @brief Get a diff between two commits identified by hash.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param hash_a Hash (or prefix) of the first commit.
 *  @param hash_b Hash (or prefix) of the second commit.
 *  @return An N_STR containing the diff text, or NULL on failure.
 */
N_STR* n_git_diff_commits(N_GIT_REPO* repo, const char* hash_a, const char* hash_b);

/*! @brief Restore a single file from HEAD (discard working directory changes).
 *  @param repo An opened N_GIT_REPO handle.
 *  @param filepath Relative path of the file to restore.
 *  @return 0 on success, -1 on failure.
 */
int n_git_checkout_path(N_GIT_REPO* repo, const char* filepath);

/*! @brief Checkout a specific commit (detached HEAD).
 *  @param repo An opened N_GIT_REPO handle.
 *  @param hash Hash (or prefix) of the commit to checkout.
 *  @return 0 on success, -1 on failure.
 */
int n_git_checkout_commit(N_GIT_REPO* repo, const char* hash);

/*! @brief List all local branch names.
 *  @param repo An opened N_GIT_REPO handle.
 *  @return A LIST of char* branch name strings, or NULL on failure.
 */
LIST* n_git_list_branches(N_GIT_REPO* repo);

/*! @brief Create a new branch from HEAD.
 *  On an empty repository (no commits yet), this updates HEAD to point
 *  to the new branch symbolically (equivalent to git checkout -b).
 *  @param repo An opened N_GIT_REPO handle.
 *  @param branch_name Name for the new branch.
 *  @return 0 on success, -1 on failure.
 */
int n_git_create_branch(N_GIT_REPO* repo, const char* branch_name);

/*! @brief Switch to an existing local branch.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param branch_name Name of the branch to switch to.
 *  @return 0 on success, -1 on failure.
 */
int n_git_switch_branch(N_GIT_REPO* repo, const char* branch_name);

/*! @brief Get the name of the current branch.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param out Buffer to receive the branch name.
 *  @param out_size Size of the output buffer.
 *  @return 0 on success, -1 on failure.
 */
int n_git_current_branch(N_GIT_REPO* repo, char* out, size_t out_size);

/*! @brief Delete a local branch.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param branch_name Name of the branch to delete.
 *  @return 0 on success, -1 on failure.
 */
int n_git_delete_branch(N_GIT_REPO* repo, const char* branch_name);

/*! @brief Set remote authentication credentials on a repo handle.
 *  The credentials are used by n_git_push and n_git_pull.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param auth Pointer to auth struct. Contents are copied (caller can free).
 *  @return 0 on success, -1 on failure.
 */
int n_git_set_remote_auth(N_GIT_REPO* repo, const N_GIT_REMOTE_AUTH* auth);

/*! @brief Ensure a remote named "origin" exists with the given URL.
 *  If "origin" exists with a different URL, it is updated.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param url Remote URL (HTTPS or SSH).
 *  @return 0 on success, -1 on failure.
 */
int n_git_remote_set_url(N_GIT_REPO* repo, const char* url);

/*! @brief Push the current branch to the "origin" remote.
 *  Uses credentials set via n_git_set_remote_auth.
 *  @param repo An opened N_GIT_REPO handle.
 *  @return 0 on success, -1 on failure.
 */
int n_git_push(N_GIT_REPO* repo);

/*! @brief Fetch from "origin" and fast-forward merge the current branch.
 *  Uses credentials set via n_git_set_remote_auth.
 *  If the merge is not a fast-forward, returns -1 (no conflict resolution).
 *  @param repo An opened N_GIT_REPO handle.
 *  @return 0 on success, -1 on failure.
 */
int n_git_pull(N_GIT_REPO* repo);

/*! @brief Fetch from the "origin" remote without merging.
 *  Uses credentials set via n_git_set_remote_auth.
 *  @param repo An opened N_GIT_REPO handle.
 *  @return 0 on success, -1 on failure.
 */
int n_git_fetch(N_GIT_REPO* repo);

/*! @brief Count commits ahead and behind relative to the remote tracking branch.
 *  Compares HEAD against refs/remotes/origin/&lt;branch-name&gt; for the current branch.
 *  @param repo An opened N_GIT_REPO handle.
 *  @param ahead Output: number of local commits not on the remote. May be NULL.
 *  @param behind Output: number of remote commits not in local. May be NULL.
 *  @return 0 on success, -1 on failure (e.g. no tracking branch).
 */
int n_git_ahead_behind(N_GIT_REPO* repo, size_t* ahead, size_t* behind);

/**
  @}
  */

#endif /* HAVE_LIBGIT2 */

#ifdef __cplusplus
}
#endif

#endif /* __N_GIT_H__ */
