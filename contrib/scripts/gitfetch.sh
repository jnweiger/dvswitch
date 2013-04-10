#! /bin/sh
##
## This is where upstream development is going on:
##
## FIXME: We should be using git archive from the remote repo
## But it does not work for me at all.
set -x

name=dvswitch
tag=0.9-1
fmt=tar.gz
git clone git://anonscm.debian.org/dvswitch/$name.git # -b $tag
cd $name
# the branch name ($2) below, can have whitespace. chop it away by 
# comparing with the non-verbose output.
# E.g.:
# * (no branch) c150b38 Upload
# 
branch__=$(git branch)
branch_v=$(git branch -vv)
commit=$(echo ${branch_v:${#branch__}} | awk '{print $1}')
archive=$name-$tag-$commit

## the / is important here!
git archive master -v --format $fmt --prefix $archive/ -o ../$archive.$fmt
git log --reverse -n 1 | cat
git branch -vv
cd ..
rm -rf $name
ls -l $archive.$fmt

