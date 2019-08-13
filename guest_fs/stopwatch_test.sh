#! /bin/sh

set -ex

CMD="/guest_fs/stopwatch_ctrl.sh"

$CMD reset
$CMD start
sleep 1
$CMD pause
RES_1=$($CMD show)

$CMD start

$CMD timeout 1
sleep 2
$CMD timeout 1
sleep 2
RES_2=$($CMD show)
RES_3=$($CMD timeout)

$CMD reset
RES_4=$($CMD show)
set +x

cat <<EOF

Results:
1) after 1s: $RES_1
2) after 5s: $RES_2
3) after irq: $RES_3
4) after reset: $RES_4
EOF

exit 0
