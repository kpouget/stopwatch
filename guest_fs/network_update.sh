#! /bin/bash

HTTP_SERVER=http://192.168.1.33:8000 # python3 -m http.server

stopwatch_test() {
    rm -f /guest_fs/stopwatch_test.sh
    wget $HTTP_SERVER/guest_fs/stopwatch_test.sh
    chmod u+x stopwatch_test.sh
}

stopwatch_ctrl() {
    rm -f /guest_fs/stopwatch_ctrl.sh
    wget $HTTP_SERVER/guest_fs/stopwatch_ctrl.sh
    chmod u+x stopwatch_ctrl.sh
}

stopwatch_module() {
    rmmod stopwatch
    rm -f stopwatch.ko
    wget $HTTP_SERVER/stopwatch.ko
    insmod stopwatch.ko
}

ACTIONS="stopwatch_test stopwatch_ctrl stopwatch_module"

help() {
    echo "Possible actions for script '$(basename $0)':"
    for action in $ACTIONS; do
        echo "    $action"
    done
    echo "    help"
}

run() {
    if [[ "$*" == *"help"* ]]; then
        help
        exit 0
    fi
    for action in $@; do
        if ! [[ "$ACTIONS" == *"$action"* ]]; then
            echo "$(basename $0): UNKNOWN ACTION: $action"
            continue
        fi
        eval $action
    done
}
