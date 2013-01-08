#!/bin/bash
# (C) 2013 see Authors.txt
#
# This file is part of MPC-HC.
#
# MPC-HC is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# MPC-HC is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

(( $1 > 0 )) || exit
# This is the last svn changeset, the number and hash can be automatically
# calculated, but it is slow to do that. So it is better to have it hardcoded.
# We'll need to update this with the last svn data before committing this script
svnrev=5597
svnhash="f669833b77e6515dc5f0a682c5bf665f9a81b2ec"

if [[ ! -d ".git" ]] || ! command -v git >/dev/null 2>&1 ; then
  # If the folder ".git" doesn't exist or git isn't available exit the script
  exit
fi

# If this revision is after the switch to git
if (( $1 > svnrev)); then
  # Calculate how many changesets we need to traverse from the svnrev to HEAD
  n=$(($1 - svnrev))

  # List the changes from svnhash..HEAD in reverse order and pick the ${n}th
  hash=$(git rev-list --reverse $svnhash..HEAD | sed -n "${n}p")
else
  # Get the hash by parsing the git-svn-id
  # --all is needed in case the given ref is not part of trunk/master but in a tag / svn branch
  hash=$(git log --all -1 --format="%H" --grep="git-svn-id: .*@$1 ")
fi

echo "$hash"
