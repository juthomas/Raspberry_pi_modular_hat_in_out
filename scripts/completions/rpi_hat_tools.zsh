#compdef output_generator input_output_tester install_rpi_dependencies.sh

_rpi_hat_output_generator() {
  _arguments -s \
    '--help[Show help and exit]' \
    '--resolution=-[Points per sine period]:points:(128 256 512 1000 2000 4000 8000 16000)' \
    '--points=-[Alias of --resolution]:points:(128 256 512 1000 2000 4000 8000 16000)' \
    '--delay-us=-[Delay between output samples in microseconds]:microseconds:(0 1 2 5 10 20 50 100 200 500 1000)'
}

_rpi_hat_input_output_tester() {
  _arguments -s \
    '--help[Show help and exit]' \
    '--resolution=-[Points per sine period]:points:(128 256 512 1000 2000 4000 8000 16000)' \
    '--points=-[Alias of --resolution]:points:(128 256 512 1000 2000 4000 8000 16000)' \
    '--delay-us=-[Delay between output samples in microseconds]:microseconds:(0 1 2 5 10 20 50 100 200 500 1000)'
}

_rpi_hat_install_script() {
  _arguments -s \
    '--help[Show help and exit]' \
    '--reboot[Reboot automatically after setup]' \
    '--baudrate[Set I2C ARM baudrate]:baudrate:(100000 400000 1000000 3400000)'
}

compdef _rpi_hat_output_generator output_generator
compdef _rpi_hat_output_generator ./output_generator
compdef _rpi_hat_output_generator C_code_example/outputs/output_generator
compdef _rpi_hat_input_output_tester input_output_tester
compdef _rpi_hat_input_output_tester ./input_output_tester
compdef _rpi_hat_input_output_tester C_code_example/input_outputs/input_output_tester
compdef _rpi_hat_install_script install_rpi_dependencies.sh
compdef _rpi_hat_install_script ./scripts/install_rpi_dependencies.sh
compdef _rpi_hat_install_script scripts/install_rpi_dependencies.sh
