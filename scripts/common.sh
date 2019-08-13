set -e

NB_CORES=4

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

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
HOME_DIR="$(realpath "$SCRIPT_DIR/..")"
