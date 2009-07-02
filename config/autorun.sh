#!/usr/bin/env bash
# Taken from lighthttpd server (BSD). Thanks Jan!
# Run this to generate all the initial makefiles, etc.

die() { echo "$@"; exit 1; }

# LIBTOOLIZE=${LIBTOOLIZE:-libtoolize}
LIBTOOLIZE_FLAGS=" --automake --copy --force"
# ACLOCAL=${ACLOCAL:-aclocal}
ACLOCAL_FLAGS="-I m4"
# AUTOHEADER=${AUTOHEADER:-autoheader}
# AUTOMAKE=${AUTOMAKE:-automake}
# --add-missing instructs automake to install missing auxiliary files
# --copy tells it to make copies and not symlinks
AUTOMAKE_FLAGS="--add-missing --copy --force"
# AUTOCONF=${AUTOCONF:-autoconf}

ARGV0=$0
ARGS="$@"


run() {
	echo "$ARGV0: running \`$@' $ARGS"
	$@ $ARGS
}

## jump out if one of the programs returns 'false'
set -e

RELEASE_DATE=`date +%Y.%m`
RELEASE_DATE_NODOTS=`date +%Y%m`
if test -d ".bzr" ; then
  echo "Grabbing changelog and version information from bzr"
  bzr log --short > ChangeLog || touch ChangeLog
  BZR_REVNO=`bzr revno`
  BZR_REVID=`bzr log -r-1 --show-ids | grep revision-id | awk '{print $2}' | head -1`
  BZR_BRANCH=`bzr nick`
else
  touch ChangeLog
  BZR_REVNO="0"
  BZR_REVID="unknown"
  BZR_BRANCH="bzr-export"
fi
RELEASE_VERSION="${RELEASE_DATE}.${BZR_REVNO}"
RELEASE_ID="${RELEASE_DATE_NODOTS}${BZR_REVNO}"
if test "x${BZR_BRANCH}" != "xdrizzle" ; then
  RELEASE_COMMENT="${BZR_BRANCH}"
else
  RELEASE_COMMENT="trunk"
fi

if test -f m4/bzr_version.m4.in
then
  sed -e "s/@BZR_REVNO@/${BZR_REVNO}/" \
      -e "s/@BZR_REVID@/${BZR_REVID}/" \
      -e "s/@BZR_BRANCH@/${BZR_BRANCH}/" \
      -e "s/@RELEASE_DATE@/${RELEASE_DATE}/" \
      -e "s/@RELEASE_ID@/${RELEASE_ID}/" \
      -e "s/@RELEASE_VERSION@/${RELEASE_VERSION}/" \
      -e "s/@RELEASE_COMMENT@/${RELEASE_COMMENT}/" \
    m4/bzr_version.m4.in > m4/bzr_version.m4.new
  
  if ! diff m4/bzr_version.m4.new m4/bzr_version.m4 >/dev/null 2>&1 ; then
    mv m4/bzr_version.m4.new m4/bzr_version.m4
  else
    rm m4/bzr_version.m4.new
  fi
fi

EGREP=`which egrep`
if test "x$EGREP" != "x" -a -d po
then
  echo "# This file is auto-generated from configure. Do not edit directly" > po/POTFILES.in.in
  # The grep -v 'drizzle-' is to exclude any distcheck leftovers
  for f in `find . | grep -v 'drizzle-' | ${EGREP} '\.(cc|c|h|yy)$' | cut -c3- | sort`
  do
    if grep gettext.h "$f" | grep include >/dev/null 2>&1
    then
      echo "$f" >> po/POTFILES.in.in
    fi
  done
  if ! diff po/POTFILES.in.in po/POTFILES.in >/dev/null 2>&1
  then
    mv po/POTFILES.in.in po/POTFILES.in
  else
    rm po/POTFILES.in.in
  fi
else
  touch po/POTFILES.in
fi

if test x$LIBTOOLIZE = x; then
  if test \! "x`which glibtoolize 2> /dev/null | grep -v '^no'`" = x; then
    LIBTOOLIZE=glibtoolize
  elif test \! "x`which libtoolize-2.2 2> /dev/null | grep -v '^no'`" = x; then
    LIBTOOLIZE=libtoolize-2.2
  elif test \! "x`which libtoolize-1.5 2> /dev/null | grep -v '^no'`" = x; then
    LIBTOOLIZE=libtoolize-1.5
  elif test \! "x`which libtoolize 2> /dev/null | grep -v '^no'`" = x; then
    LIBTOOLIZE=libtoolize
  elif test \! "x`which glibtoolize 2> /dev/null | grep -v '^no'`" = x; then
    LIBTOOLIZE=glibtoolize
  else
    echo "libtoolize wasn't found, exiting"; exit 1
  fi
fi

if test x$ACLOCAL = x; then
  if test \! "x`which aclocal-1.10 2> /dev/null | grep -v '^no'`" = x; then
    ACLOCAL=aclocal-1.10
  elif test \! "x`which aclocal110 2> /dev/null | grep -v '^no'`" = x; then
    ACLOCAL=aclocal110
  elif test \! "x`which aclocal 2> /dev/null | grep -v '^no'`" = x; then
    ACLOCAL=aclocal
  else 
    echo "automake 1.10.x (aclocal) wasn't found, exiting"; exit 1
  fi
fi

if test x$AUTOMAKE = x; then
  if test \! "x`which automake-1.10 2> /dev/null | grep -v '^no'`" = x; then
    AUTOMAKE=automake-1.10
  elif test \! "x`which automake110 2> /dev/null | grep -v '^no'`" = x; then
    AUTOMAKE=automake110
  elif test \! "x`which automake 2> /dev/null | grep -v '^no'`" = x; then
    AUTOMAKE=automake
  else 
    echo "automake 1.10.x wasn't found, exiting"; exit 1
  fi
fi


## macosx has autoconf-2.59 and autoconf-2.60
if test x$AUTOCONF = x; then
  if test \! "x`which autoconf-2.63 2> /dev/null | grep -v '^no'`" = x; then
    AUTOCONF=autoconf-2.63
  elif test \! "x`which autoconf263 2> /dev/null | grep -v '^no'`" = x; then
    AUTOCONF=autoconf263
  elif test \! "x`which autoconf-2.60 2> /dev/null | grep -v '^no'`" = x; then
    AUTOCONF=autoconf-2.60
  elif test \! "x`which autoconf260 2> /dev/null | grep -v '^no'`" = x; then
    AUTOCONF=autoconf260
  elif test \! "x`which autoconf-2.59 2> /dev/null | grep -v '^no'`" = x; then
    AUTOCONF=autoconf-2.59
  elif test \! "x`which autoconf259 2> /dev/null | grep -v '^no'`" = x; then
    AUTOCONF=autoconf259
  elif test \! "x`which autoconf 2> /dev/null | grep -v '^no'`" = x; then
    AUTOCONF=autoconf
  else 
    echo "autoconf 2.59+ wasn't found, exiting"; exit 1
  fi
fi

if test x$AUTOHEADER = x; then
  if test \! "x`which autoheader-2.63 2> /dev/null | grep -v '^no'`" = x; then
    AUTOHEADER=autoheader-2.63
  elif test \! "x`which autoheader263 2> /dev/null | grep -v '^no'`" = x; then
    AUTOHEADER=autoheader263
  elif test \! "x`which autoheader-2.60 2> /dev/null | grep -v '^no'`" = x; then
    AUTOHEADER=autoheader-2.60
  elif test \! "x`which autoheader260 2> /dev/null | grep -v '^no'`" = x; then
    AUTOHEADER=autoheader260
  elif test \! "x`which autoheader-2.59 2> /dev/null | grep -v '^no'`" = x; then
    AUTOHEADER=autoheader-2.59
  elif test \! "x`which autoheader259 2> /dev/null | grep -v '^no'`" = x; then
    AUTOHEADER=autoheader259
  elif test \! "x`which autoheader 2> /dev/null | grep -v '^no'`" = x; then
    AUTOHEADER=autoheader
  else 
    echo "autoconf 2.59+ (autoheader) wasn't found, exiting"; exit 1
  fi
fi


# --force means overwrite ltmain.sh script if it already exists 
run python config/register_plugins.py || die  "Can't execute register_plugins"
run $LIBTOOLIZE $LIBTOOLIZE_FLAGS || die "Can't execute libtoolize"

run $ACLOCAL $ACLOCAL_FLAGS || die "Can't execute aclocal"
run $AUTOHEADER || die "Can't execute autoheader"
run $AUTOMAKE $AUTOMAKE_FLAGS  || die "Can't execute automake"
run $AUTOCONF || die "Can't execute autoconf"
echo -n "Automade with: "
$AUTOMAKE --version | head -1
echo -n "Configured with: "
$AUTOCONF --version | head -1
