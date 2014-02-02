#!/bin/bash

GLFTPD_LOG=/glftpd/ftp-data/logs/glftpd.log
PREBW_BINARY=/glftpd/bin/prebw

watch_log() {
    tail -f -n0 $GLFTPD_LOG | while read line; do
        if [ "$(echo $line | cut -d ' ' -f 6 | cut -d ':' -f 1)" == "PRE" ]; then
            $PREBW_BINARY $(echo $line | cut -d '"' -f 2) &
        fi
    done
}

watch_log &
