#! /bin/bash

SCRIPTNAME=$0
MODE=$1

## so the tests can complain if you fail to use the script to launch them
export DBUS_TEST_GLIB_RUN_TEST_SCRIPT=1

# Rerun ourselves with tmp session bus if we're not already
if test -z "$DBUS_TEST_GLIB_IN_RUN_TEST"; then
  DBUS_TEST_GLIB_IN_RUN_TEST=1
  export DBUS_TEST_GLIB_IN_RUN_TEST
  exec $DBUS_TOP_BUILDDIR/tools/run-with-tmp-session-bus.sh $SCRIPTNAME $MODE
fi  

if test x$MODE = xprofile ; then
  echo "profiling type $PROFILE_TYPE"
  sleep 2 ## this lets the bus get started so its startup time doesn't affect the profile too much
  if test x$PROFILE_TYPE = x ; then
      PROFILE_TYPE=all
  fi
  libtool --mode=execute $DEBUG $DBUS_TOP_BUILDDIR/test/glib/test-profile $PROFILE_TYPE || die "test-profile failed"
elif test x$MODE = xviewer ; then
  echo "Launching dbus-viewer"
  ARGS=
  if test x$DEBUG = x ; then
      ARGS="--services org.freedesktop.DBus org.freedesktop.DBus.TestSuiteGLibService"
  fi
  libtool --mode=execute $DEBUG $DBUS_TOP_BUILDDIR/tools/dbus-viewer $ARGS || die "could not run dbus-viewer"
elif test x$MODE = xwait ; then
  echo "Waiting DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS"
  sleep 86400
else
  echo "running test-dbus-glib"
  libtool --mode=execute $DEBUG $DBUS_TOP_BUILDDIR/test/glib/test-dbus-glib || die "test-dbus-glib failed"
fi
