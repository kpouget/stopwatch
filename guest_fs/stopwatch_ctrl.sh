#! /bin/sh

# see driver/stopwatch_hw-sw.h for the value->action mapping


case $1 in
    reset)
        echo 0 > /sys/module/stopwatch/parameters/stopwatch_status
        RET=$?
        break;;
    start)
        echo 1 > /sys/module/stopwatch/parameters/stopwatch_status
        RET=$?
        break;;
    pause)
        echo 2 > /sys/module/stopwatch/parameters/stopwatch_status
        RET=$?
        break;;
    show)
        #echo 3 > /sys/module/stopwatch/parameters/stopwatch_status
        cat /sys/kernel/debug/stopwatch/stopwatch
        RET=$?
        break;;
    timeout)
        #echo 4 > /sys/module/stopwatch/parameters/stopwatch_status
        NB_SECONDS=$2
        if [ -z "$NB_SECONDS" ]; then
            NB_IRQ=$(cat /sys/module/stopwatch/parameters/stopwatch_timeout)
            RET=$?
            echo "Number of IRQ ticks: $NB_IRQ"
        elif ! [ $NB_SECONDS -eq $NB_SECONDS ]; then
            echo "Invalid timeout ($NB_SECONDS), must be numeric ..."
            RET=2;
        else
            echo $NB_SECONDS > /sys/module/stopwatch/parameters/stopwatch_timeout
            RET=$?
        fi
        break;;
    *)
        cat <<EOF
Usage: $0 action
Action:
  - reset: resets the stopwatch
  - start: start the stopwatch counter
  - pause: pauses the stopwatch counter
  - show: get the stopwatch count value
  - timeout:shows the number of ticks fo the timeout IRQ
  - timeout time: triggers an IRQ aftert \$time seconds
EOF
    RET=3;
        ;;
esac

exit $RET
