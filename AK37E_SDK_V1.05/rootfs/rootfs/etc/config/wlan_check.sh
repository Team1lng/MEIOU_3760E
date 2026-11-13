#! /bin/sh
function poke {
while true
    do
        printf '\n'
        sleep 1
    done
}

function watch {
    (poke) | wpa_cli | while read line
    do
        case "$line" in
            *'4-Way Handshake failed'*)
                echo "incorrect key"
                return
            ;;
            *'CTRL-EVENT-CONNECTED'*)
                echo "connected"
                return
            ;;
             *'CTRL-EVENT-NETWORK-NOT-FOUND'*)
                echo "not found"
                return
            ;;
        esac
    done
}

wpa_cli disable_network 0 > /dev/null
wpa_cli enable_network 0 > /dev/null

watch