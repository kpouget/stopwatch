#! /bin/sh

if cat /proc/cmdline | grep -q "stopwatch=test"; then
    echo "Running /guest_fs/stopwatch_test.sh"
    /guest_fs/stopwatch_test.sh
    RET=$?
fi

if cat /proc/cmdline | grep -q "stopwatch=test_and_quit"; then
    echo "Bye bye! (return code: $RET)"
    echo o > /proc/sysrq-trigger
fi
