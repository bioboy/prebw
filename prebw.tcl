namespace eval ::ngBot::plugin::prebw {
    variable binary "$::ngBot::glroot/bin/prebw"
    variable events [list "PRE"]

    variable ns [namespace current]
    variable np [namespace qualifiers [namespace parent]]

    variable scriptname ${ns}::log_event

    proc init {} {
        variable ns
        variable np
        variable events
        variable scriptname

        variable ${np}::precommand

        foreach event $events {
            lappend precommand($event) $scriptname
        }
    }

    proc deinit {} {
        variable ns
        variable np
        variable events
        variable scriptname

        variable ${np}::precommand

        foreach event $events {
            if {[info exists precommand($event)] && [set pos [lsearch -exact $precommand($event) $scriptname]] !=  -1} {
                set precommand($event) [lreplace $precommand($event) $pos $pos]
            }

            namespace delete $ns
        }
    }

    proc log_event { event section logdata } {
        variable binary
        variable events
        if {[lsearch -nocase $events $event] != -1} {
            exec $binary [lindex $logdata 0] &
        }
        return 1
    }
}
