#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#
# This tool generates incremental update packages for the update system.
# Author: Darin Fisher
#

. $(dirname "$0")/common.sh

# -----------------------------------------------------------------------------

print_usage() {
  notice "Usage: $(basename $0) [OPTIONS] ARCHIVE FROMDIR TODIR"
  notice ""
  notice "The differences between FROMDIR and TODIR will be stored in ARCHIVE."
  notice ""
  notice "Options:"
  notice "  -h  show this help text"
  notice "  -f  clobber this file in the installation"
  notice "      Must be a path to a file to clobber in the partial update."
  notice "  -q  be less verbose"
  notice ""
}

check_for_forced_update() {
  force_list="$1"
  forced_file_chk="$2"

  local f

  if [ "$forced_file_chk" = "precomplete" ]; then
    ## "true" *giggle*
    return 0;
  fi

  if [ "${forced_file_chk##*.}" = "chk" ]
  then
    ## "true" *giggle*
    return 0;
  fi

  for f in $force_list; do
    #echo comparing $forced_file_chk to $f
    if [ "$forced_file_chk" = "$f" ]; then
      ## "true" *giggle*
      return 0;
    fi

    # If the file in the skip list ends with /*, do a prefix match.
    # This allows TorBrowser/Data/Browser/profile.default/extensions/https-everywhere@eff.org/* to be used to force all HTTPS Everywhere files to be updated.
    f_suffix=${f##*/}
    if [[ $f_suffix = "*" ]]; then
      f_prefix="${f%\/\*}";
      if [[ $forced_file_chk == $f_prefix* ]]; then
        ## 0 means "true"
        return 0;
      fi
    fi
  done
  ## 'false'... because this is bash. Oh yay!
  return 1;
}

if [ $# = 0 ]; then
  print_usage
  exit 1
fi

# The last .xpi is NoScript.
ext_path='TorBrowser/Data/Browser/profile.default/extensions';
# TODO: it would be better to pass this as a command line option.
exts='https-everywhere@eff.org/* tor-launcher@torproject.org.xpi torbutton@torproject.org.xpi uriloader@pdf.js.xpi {73a6fe31-595d-460b-a920-fcc0f8843232}.xpi'
requested_forced_updates='Contents/MacOS/TorBrowser.app/Contents/MacOS/firefox'
for ext in $exts; do
  requested_forced_updates="$requested_forced_updates $ext_path/$ext"
done


# TODO: it would be better to pass this as a command line option.
directories_to_remove='TorBrowser/Data/Browser/profile.default/extensions/https-everywhere@eff.org'

while getopts "hqf:" flag
do
   case "$flag" in
      h) print_usage; exit 0
      ;;
      q) QUIET=1
      ;;
      f) requested_forced_updates="$requested_forced_updates $OPTARG"
      ;;
      ?) print_usage; exit 1
      ;;
   esac
done

# -----------------------------------------------------------------------------

let arg_start=$OPTIND-1
shift $arg_start

archive="$1"
olddir="$2"
newdir="$3"
# Prevent the workdir from being inside the targetdir so it isn't included in
# the update mar.
if [ $(echo "$newdir" | grep -c '\/$') = 1 ]; then
  # Remove the /
  newdir=$(echo "$newdir" | sed -e 's:\/$::')
fi
workdir="$newdir.work"
updatemanifestv2="$workdir/updatev2.manifest"
updatemanifestv3="$workdir/updatev3.manifest"
archivefiles="updatev2.manifest updatev3.manifest"

mkdir -p "$workdir"

# On Mac, the precomplete file added by Bug 386760 will cause OS X to reload the
# Info.plist so it launches the right architecture, bug 600098

# Generate a list of all files in the target directory.
pushd "$olddir"
if test $? -ne 0 ; then
  exit 1
fi

list_files oldfiles
list_symlinks oldsymlinks oldsymlink_targets
list_dirs olddirs

popd

pushd "$newdir"
if test $? -ne 0 ; then
  exit 1
fi

if [ ! -f "precomplete" ]; then
  notice "precomplete file is missing!"
  exit 1
fi

list_dirs newdirs
list_files newfiles
list_symlinks newsymlinks newsymlink_targets

popd

# Add the type of update to the beginning of the update manifests.
notice ""
notice "Adding type instruction to update manifests"
> "$updatemanifestv2"
> "$updatemanifestv3"
notice "       type partial"
echo "type \"partial\"" >> "$updatemanifestv2"
echo "type \"partial\"" >> "$updatemanifestv3"

# If removal of any old, existing directories is desired, emit the appropriate
# rmrfdir commands.
notice ""
notice "Adding directory removal instructions to update manifests"
for dir_to_remove in $directories_to_remove; do
  # rmrfdir requires a trailing slash, so add one if missing.
  if ! [[ "$dir_to_remove" =~ /$ ]]; then
    dir_to_remove="${dir_to_remove}/"
  fi
  echo "rmrfdir \"$dir_to_remove\"" >> "$updatemanifestv2"
  echo "rmrfdir \"$dir_to_remove\"" >> "$updatemanifestv3"
done

notice ""
notice "Adding file patch and add instructions to update manifests"

num_oldfiles=${#oldfiles[*]}
remove_array=
num_removes=0

for ((i=0; $i<$num_oldfiles; i=$i+1)); do
  f="${oldfiles[$i]}"

  # This file is created by Talkback, so we can ignore it
  if [ "$f" = "readme.txt" ]; then
    continue 1
  fi

  # removed-files is excluded by make_incremental_updates.py so it is excluded
  # here for consistency.
  if [ "`basename "$f"`" = "removed-files" ]; then
    continue 1
  fi

  # If this file exists in the new directory as well, then check if it differs.
  if [ -f "$newdir/$f" ]; then

    if check_for_add_if_not_update "$f"; then
      # The full workdir may not exist yet, so create it if necessary.
      mkdir -p `dirname "$workdir/$f"`
      $BZIP2 -cz9 "$newdir/$f" > "$workdir/$f"
      copy_perm "$newdir/$f" "$workdir/$f"
      make_add_if_not_instruction "$f" "$updatemanifestv3"
      archivefiles="$archivefiles \"$f\""
      continue 1
    fi

    if check_for_forced_update "$requested_forced_updates" "$f"; then
      # The full workdir may not exist yet, so create it if necessary.
      mkdir -p `dirname "$workdir/$f"`
      $BZIP2 -cz9 "$newdir/$f" > "$workdir/$f"
      copy_perm "$newdir/$f" "$workdir/$f"
      make_add_instruction "$f" "$updatemanifestv2" "$updatemanifestv3" 1
      archivefiles="$archivefiles \"$f\""
      continue 1
    fi

    if ! diff "$olddir/$f" "$newdir/$f" > /dev/null; then
      # Compute both the compressed binary diff and the compressed file, and
      # compare the sizes.  Then choose the smaller of the two to package.
      dir=$(dirname "$workdir/$f")
      mkdir -p "$dir"
      notice "diffing \"$f\""
      $MBSDIFF "$olddir/$f" "$newdir/$f" "$workdir/$f.patch"
      $BZIP2 -z9 "$workdir/$f.patch"
      $BZIP2 -cz9 "$newdir/$f" > "$workdir/$f"
      copy_perm "$newdir/$f" "$workdir/$f"
      patchfile="$workdir/$f.patch.bz2"
      patchsize=$(get_file_size "$patchfile")
      fullsize=$(get_file_size "$workdir/$f")

      if [ $patchsize -lt $fullsize ]; then
        make_patch_instruction "$f" "$updatemanifestv2" "$updatemanifestv3"
        mv -f "$patchfile" "$workdir/$f.patch"
        rm -f "$workdir/$f"
        archivefiles="$archivefiles \"$f.patch\""
      else
        make_add_instruction "$f" "$updatemanifestv2" "$updatemanifestv3"
        rm -f "$patchfile"
        archivefiles="$archivefiles \"$f\""
      fi
    fi
  else
    # remove instructions are added after add / patch instructions for
    # consistency with make_incremental_updates.py
    remove_array[$num_removes]=$f
    (( num_removes++ ))
  fi
done

# Remove and re-add symlinks
notice ""
notice "Adding symlink remove/add instructions to file 'updatev3.manifest'"
num_oldsymlinks=${#oldsymlinks[*]}
for ((i=0; $i<$num_oldsymlinks; i=$i+1)); do
  link="${oldsymlinks[$i]}"
  verbose_notice "        remove: $link"
  echo "remove \"$link\"" >> "$updatemanifestv3"
done

num_newsymlinks=${#newsymlinks[*]}
for ((i=0; $i<$num_newsymlinks; i=$i+1)); do
  link="${newsymlinks[$i]}"
  target="${newsymlink_targets[$i]}"
  make_addsymlink_instruction "$link" "$target" "$updatemanifestv3"
done

# Newly added files
notice ""
notice "Adding file add instructions to update manifests"
num_newfiles=${#newfiles[*]}

for ((i=0; $i<$num_newfiles; i=$i+1)); do
  f="${newfiles[$i]}"

  # removed-files is excluded by make_incremental_updates.py so it is excluded
  # here for consistency.
  if [ "`basename "$f"`" = "removed-files" ]; then
    continue 1
  fi

  # If we've already tested this file, then skip it
  for ((j=0; $j<$num_oldfiles; j=$j+1)); do
    if [ "$f" = "${oldfiles[j]}" ]; then
      continue 2
    fi
  done

  dir=$(dirname "$workdir/$f")
  mkdir -p "$dir"

  $BZIP2 -cz9 "$newdir/$f" > "$workdir/$f"
  copy_perm "$newdir/$f" "$workdir/$f"

  if check_for_add_if_not_update "$f"; then
    make_add_if_not_instruction "$f" "$updatemanifestv3"
  else
    make_add_instruction "$f" "$updatemanifestv2" "$updatemanifestv3"
  fi


  archivefiles="$archivefiles \"$f\""
done

notice ""
notice "Adding file remove instructions to update manifests"
for ((i=0; $i<$num_removes; i=$i+1)); do
  f="${remove_array[$i]}"
  notice "     remove \"$f\""
  echo "remove \"$f\"" >> "$updatemanifestv2"
  echo "remove \"$f\"" >> "$updatemanifestv3"
done

# Add remove instructions for any dead files.
notice ""
notice "Adding file and directory remove instructions from file 'removed-files'"
append_remove_instructions "$newdir" "$updatemanifestv2" "$updatemanifestv3"

notice ""
notice "Adding directory remove instructions for directories that no longer exist"
num_olddirs=${#olddirs[*]}

for ((i=0; $i<$num_olddirs; i=$i+1)); do
  f="${olddirs[$i]}"
  # If this dir doesn't exist in the new directory remove it.
  if [ ! -d "$newdir/$f" ]; then
    notice "      rmdir $f/"
    echo "rmdir \"$f/\"" >> "$updatemanifestv2"
    echo "rmdir \"$f/\"" >> "$updatemanifestv3"
  fi
done

$BZIP2 -z9 "$updatemanifestv2" && mv -f "$updatemanifestv2.bz2" "$updatemanifestv2"
$BZIP2 -z9 "$updatemanifestv3" && mv -f "$updatemanifestv3.bz2" "$updatemanifestv3"

eval "$MAR -C \"$workdir\" -c output.mar $archivefiles"
mv -f "$workdir/output.mar" "$archive"

# cleanup
rm -fr "$workdir"

notice ""
notice "Finished"
notice ""
