# SPDX-License-Identifier: GPL-2.0
#!/bin/bash
#
# Copyright (C) Google LLC, 2022
# Author: Will McVicker (willmcvicker@google.com)

GKI_REMOTE="aosp"
GKI_SHA=`repo --color=never info aosp | grep "Manifest revision" | sed 's/Manifest revision: //g'`
GKI_BRANCH="android14-5.15" # Need to push symbol list changes to the main ACK branch (not release branches)
GKI_STAGING_REMOTE="partner-common"
GKI_STAGING_BRANCH=`repo --color=never info aosp-staging | grep "Manifest revision" | sed 's/Manifest revision: //g'`
TARGET=
FOR_AOSP_PUSH_BRANCH="update_symbol_list-delete-after-push"
CONTINUE_AFTER_REBASE=0
CHANGE_ID=
BUG=

function usage {
  ret="$1"

  echo "$0 --config TARGET [-p|--prepare-aosp-abi BUG_NUMBER] [--change-id CHANGE_ID] [--continue]"
  echo
  echo "This script will update the pixel symbol list."
  echo
  echo " The following arguments are supported:"
  echo "  --config <device-name>               Specifies which target to build."
  echo "  -p | --prepare-aosp-abi BUG_NUMBER   Update the AOSP ABI xml and symbol list."
  echo "                                       and create a commit with the provided BUG_NUMBER."
  echo "  -c | --continue                      Continue after the rebase failure."
  echo "  --change-id CHANGE_ID                Use this Change-Id when creating the commit."
  echo "  --commit BUG_NUMBER                  [DEPRECATED to keep same flag as 5.10] same as -p|--prepare-aosp-abi"
  exit ${ret}
}

# Add a trap to remove the temporary files in case of an error on early exit.
cleanup_trap() {
  rm -f ${TMP_LIST} ${COMMIT_TEXT}
  exit $1
}
trap 'cleanup_trap' EXIT

function exit_if_error {
  if [ $1 -ne 0 ]; then
    echo "ERROR: $2: retval=$1" >&2
    exit $1
  fi
}


while [[ $# -gt 0 ]]; do
  next="$1"
  case ${next} in
  --config)
    TARGET="$2"
    shift
    ;;
  -p|--prepare-aosp-abi|--commit)
    BUG="$2"
    if ! [[ "${BUG}" =~ ^[0-9]+$ ]]; then
      exit_if_error 1 "Bug numbers should be digits."
    fi
    shift
    ;;
  -c|--continue)
    CONTINUE_AFTER_REBASE=1
    ;;
  --change-id)
    CHANGE_ID="$2"
    if ! [[ "${CHANGE_ID}" =~ ^I[0-9a-f]{40}$ ]]; then
      exit_if_error 1 \
        "Invalid Change-Id. Make sure it starts with 'I' followed by 40 hex characters"
    fi
    shift
    ;;
  -h|--help)
    usage 0
    ;;
  *)
    echo "Invalid argument $1"
    usage 1
    ;;
  esac
  shift
done

# Enforce the target selection. Needs to be run at the repo root
if [[ -z "${TARGET}" ]] || [[ ! -d "private/devices/google/${TARGET}" ]]; then
  echo "Invalid target: ${TARGET}"
  usage 1
fi

BASE_KERNEL=$(tools/bazel cquery filter\(kernel_aarch64_sources, deps\(//private/devices/google/${TARGET}:zuma_${TARGET}\)\) 2>/dev/null --config=${TARGET})
if [[ "${BASE_KERNEL}" =~ aosp-staging ]]; then
  KERNEL_DIR="aosp-staging/"
else
  KERNEL_DIR="aosp/"
fi


function verify_aosp_tree {
  pushd aosp >/dev/null
    if ! git diff --quiet HEAD; then
      exit_if_error 1 \
        "Found uncommitted changes in aosp/. Commit your changes before updating the ABI"
    fi

    if [ "${CONTINUE_AFTER_REBASE}" = "0" ]; then
      if git branch | grep "\<${FOR_AOSP_PUSH_BRANCH}\>" 2>&1 >/dev/null; then
        echo "The branch '${FOR_AOSP_PUSH_BRANCH}' already exists in aosp/. Please delete" >&2
        echo "this branch (git branch -D ${FOR_AOSP_PUSH_BRANCH}) before continuing." >&2
        exit 1
      fi

      AOSP_CUR_BRANCH_OR_SHA1=$(git branch --show-current)
      if [ -z "${AOSP_CUR_BRANCH_OR_SHA1}" ]; then
        AOSP_CUR_BRANCH_OR_SHA1=$(git log -1 --pretty="format:%H")
      fi
    else
      # Make sure they didn't switch branches when addressing the rebase conflict
      if [ "${FOR_AOSP_PUSH_BRANCH}" != "$(git branch --show-current)" ]; then
        exit_if_error 1 "For --continue, you need to be on the branch ${FOR_AOSP_PUSH_BRANCH}"
      fi
    fi
  popd >/dev/null
}

function print_final_message {
  echo "========================================================"
  if ! git -C aosp diff --quiet ${GKI_REMOTE}/${GKI_BRANCH}..HEAD; then
    echo " A symbol list commit in aosp/ was created for you."
    echo
    echo " Please verify your commit(s) before pushing. Here are the steps to perform:"
    echo
    echo "   cd aosp"
    echo "   git log --oneline ${FOR_AOSP_PUSH_BRANCH}"
    echo "   git push ${GKI_REMOTE} ${FOR_AOSP_PUSH_BRANCH:-HEAD}:refs/for/${GKI_BRANCH}"
    echo
    if [ -n "${FOR_AOSP_PUSH_BRANCH}" ]; then
      echo " After pushing your changes to aosp/, you can delete the temporary"
      echo " branch: ${FOR_AOSP_PUSH_BRANCH} using the command:"
      echo
      echo "   cd aosp"
      echo "   git branch -D ${FOR_AOSP_PUSH_BRANCH}"
      echo
    fi
  else
    echo " No changes were detected after rebasing to the tip of tree."
  fi

  # Rollback to the original branch/commit
  if [ -n "${AOSP_CUR_BRANCH_OR_SHA1}" ]; then
    git -C aosp checkout --quiet ${AOSP_CUR_BRANCH_OR_SHA1}
  fi
}

# Update the AOSP symbol list
# $1 the base kernel
# $2 the aosp kernel symbol list
function apply_to_aosp_symbol_list {
  TMP_LIST=$(mktemp -t symbol_list.XXXX)
  cp -f $2 ${TMP_LIST}

  # Only apply the new symbol additions. This makes sure that we don't copy
  # over any symbols that are only found in the aosp-staging branch.
  git -C $1 diff ${GKI_STAGING_REMOTE}/${GKI_STAGING_BRANCH}..HEAD | grep "^+  " >> ${TMP_LIST}

  # Remove leading plus signs from the `git show`
  sed -i 's:^+  \(.\+\):  \1:g' ${TMP_LIST}

  # Remove empty lines and comments
  sed -i '/^$/d' ${TMP_LIST}
  sed -i '/^#/d' ${TMP_LIST}
  LC_ALL=en_US.utf8 sort -ubfi ${TMP_LIST} > $2

  rm -f ${TMP_LIST}
}

function commit_the_symbol_list {
  local aosp_dir="$1"
  local pixel_symbol_list="android/abi_gki_aarch64_pixel"

  echo "Committing symbol list: ${aosp_dir}"

  NEW_SYMS=$(git -C "${aosp_dir}" diff ${pixel_symbol_list} 2>/dev/null | sed -n 's/^+\s\+\(.*\)/\1\n/p')
  OLD_SYMS=$(git -C "${aosp_dir}" diff ${pixel_symbol_list} 2>/dev/null | sed -n 's/^-\s\+\(.*\)/\1\n/p')

  ADDING=$(for s in ${NEW_SYMS}; do [[ ! "${OLD_SYMS}" =~ ${s} ]] && echo "${s}"; done)
  REMOVING=$(for s in ${OLD_SYMS}; do [[ ! "${NEW_SYMS}" =~ ${s} ]] && echo "${s}"; done)

  # Create the symbol list commit
  COMMIT_TEXT=$(mktemp -t abi_sym_commit_text.XXXXX)
  echo "ANDROID: Update the ABI symbol list" > ${COMMIT_TEXT}
  echo >> ${COMMIT_TEXT}
  if [ -n "${ADDING}" ]; then
    echo "Adding the following symbols:" >> ${COMMIT_TEXT}
    for s in ${ADDING}; do
      echo "  - $s" >> ${COMMIT_TEXT}
    done
    echo >> ${COMMIT_TEXT}
  fi
  if [ -n "${REMOVING}" ]; then
    echo "Removing the following symbols:" >> ${COMMIT_TEXT}
    for s in ${REMOVING}; do
      echo "  - $s" >> ${COMMIT_TEXT}
    done
    echo >> ${COMMIT_TEXT}
  fi

  echo "Bug: ${BUG}" >> ${COMMIT_TEXT}
  if [ -n "${CHANGE_ID}" ]; then
    echo "Change-Id: ${CHANGE_ID}" >> ${COMMIT_TEXT}
  fi
  git -C "${aosp_dir}" commit --quiet -s -F ${COMMIT_TEXT} -- "${pixel_symbol_list}"
  if [[ "$?" != 0 ]] && [[ ${aosp_dir} =~ aosp-staging ]]; then
    rm -f ${COMMIT_TEXT}
    echo "No symbol list changes detected in ${aosp_dir}."
    exit 0
  fi
  echo "done..."
  rm -f ${COMMIT_TEXT}
}

function update_aosp_to_tot {
  local pixel_symbol_list="android/abi_gki_aarch64_pixel"

  # Rebase to ${GKI_REMOTE}/${GKI_BRANCH} ToT before copying over the symbol list
  pushd aosp/ >/dev/null
    if [ "${CONTINUE_AFTER_REBASE}" = "0" ]; then
      git checkout --quiet -b ${FOR_AOSP_PUSH_BRANCH}
    fi
    git fetch --quiet ${GKI_REMOTE} ${GKI_BRANCH} && git rebase --quiet FETCH_HEAD
    err=$?
    if [ "${err}" != "0" ]; then
      echo "ERROR: Failed to rebase your aosp/ change(s) to the AOSP ToT." >&2
      echo "To resolve this, please manually resolve the rebase conflicts" >&2
      echo "and run: git rebase --continue. Then resume this script" >&2
      echo "using the command:" >&2
      echo >&2
      echo "  $0 --bug ${BUG} --continue" >&2
      echo >&2
      echo "To return to your original tree in aosp/ after finishing the" >&2
      echo "symbol list update, run this git command:" >&2
      echo >&2
      echo "  git checkout ${AOSP_CUR_BRANCH_OR_SHA1}" >&2
      echo >&2
      exit 1
    fi
  popd >/dev/null

  if [[ "${BASE_KERNEL}" =~ aosp-staging ]]; then
    # Since we are using aosp-staging, we need to update the AOSP symbol list
    # too.
    #
    # First, rollback any symbol list changes in aosp/ and then apply the
    # aosp-staging symbol list diff to the aosp version of the pixel symbol
    # list. This ensures that we only add symbols needed based on the current
    # pixel changes.
    #
    # Note: we are NOT copying over the aosp-staging/ symbol list to the aosp/
    # symbol list in order to avoid pulling in symbols that only exist on the
    # aosp-staging branch.
    git -C aosp show --quiet ${GKI_REMOTE}/${GKI_BRANCH}:"${pixel_symbol_list}" \
      > aosp/"${pixel_symbol_list}"
    apply_to_aosp_symbol_list ${KERNEL_DIR} "aosp/${pixel_symbol_list}"

    # Create the AOSP symbol list commit
    commit_the_symbol_list "aosp"
  fi
}


# Verify the aosp tree is in a good state before compiling anything
verify_aosp_tree

if [ "${CONTINUE_AFTER_REBASE}" = "0" ]; then
  # Update the symbol list now
  tools/bazel run --config=${TARGET} --config=fast //private/devices/google/${TARGET}:zuma_${TARGET}_abi_update_symbol_list
  exit_if_error $? "Failed to update the ${TARGET} symbol list"

  if [ -z "${BUG}" ]; then
    # Not committing the change
    echo
    echo "The symbol list in ${KERNEL_DIR} was updated. If you want to commit this to AOSP,"
    echo "then re-run this script with the --commit BUG_NUMBER command line argument."
    exit 0
  fi

  commit_the_symbol_list ${KERNEL_DIR}
fi

# Rebase the symbol list change to the AOSP ToT
update_aosp_to_tot

print_final_message
