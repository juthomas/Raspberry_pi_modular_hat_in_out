#!/usr/bin/env bash

_rpi_hat_complete_output_generator() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ "$cur" == --resolution=* ]]; then
        COMPREPLY=($(compgen -W "--resolution=128 --resolution=256 --resolution=512 --resolution=1000 --resolution=2000 --resolution=4000 --resolution=8000 --resolution=16000" -- "$cur"))
        return
    fi
    if [[ "$cur" == --points=* ]]; then
        COMPREPLY=($(compgen -W "--points=128 --points=256 --points=512 --points=1000 --points=2000 --points=4000 --points=8000 --points=16000" -- "$cur"))
        return
    fi
    if [[ "$cur" == --delay-us=* ]]; then
        COMPREPLY=($(compgen -W "--delay-us=0 --delay-us=1 --delay-us=2 --delay-us=5 --delay-us=10 --delay-us=20 --delay-us=50 --delay-us=100 --delay-us=200 --delay-us=500 --delay-us=1000" -- "$cur"))
        return
    fi
    COMPREPLY=($(compgen -W "--help --resolution= --points= --delay-us=" -- "$cur"))
}

_rpi_hat_complete_input_output_tester() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ "$cur" == --resolution=* ]]; then
        COMPREPLY=($(compgen -W "--resolution=128 --resolution=256 --resolution=512 --resolution=1000 --resolution=2000 --resolution=4000 --resolution=8000 --resolution=16000" -- "$cur"))
        return
    fi
    if [[ "$cur" == --points=* ]]; then
        COMPREPLY=($(compgen -W "--points=128 --points=256 --points=512 --points=1000 --points=2000 --points=4000 --points=8000 --points=16000" -- "$cur"))
        return
    fi
    if [[ "$cur" == --delay-us=* ]]; then
        COMPREPLY=($(compgen -W "--delay-us=0 --delay-us=1 --delay-us=2 --delay-us=5 --delay-us=10 --delay-us=20 --delay-us=50 --delay-us=100 --delay-us=200 --delay-us=500 --delay-us=1000" -- "$cur"))
        return
    fi
    COMPREPLY=($(compgen -W "--help --resolution= --points= --delay-us=" -- "$cur"))
}

_rpi_hat_complete_install_script() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    if [[ "$prev" == "--baudrate" ]]; then
        COMPREPLY=($(compgen -W "100000 400000 1000000 3400000" -- "$cur"))
        return
    fi

    COMPREPLY=($(compgen -W "--help --reboot --baudrate" -- "$cur"))
}

complete -F _rpi_hat_complete_output_generator output_generator
complete -F _rpi_hat_complete_output_generator ./output_generator
complete -F _rpi_hat_complete_output_generator C_code_example/outputs/output_generator
complete -F _rpi_hat_complete_input_output_tester input_output_tester
complete -F _rpi_hat_complete_input_output_tester ./input_output_tester
complete -F _rpi_hat_complete_input_output_tester C_code_example/input_outputs/input_output_tester
complete -F _rpi_hat_complete_install_script install_rpi_dependencies.sh
complete -F _rpi_hat_complete_install_script ./scripts/install_rpi_dependencies.sh
complete -F _rpi_hat_complete_install_script scripts/install_rpi_dependencies.sh
