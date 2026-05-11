./scripts/exec_into_docker_with_build.sh

source /user/scripts/setup_emulated_ssds.sh 4 /user

cd /user/build/examples
./multi_device_example /dev/kvemul0 /dev/kvemul1 /dev/kvemul2




./multi_device_example /dev/kvemul0 /dev/kvemul1

./multi_device_example /dev/kvemul0 /dev/kvemul1 /dev/kvemul2 /dev/kvemul3
