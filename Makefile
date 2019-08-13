DOCKER_CMD := sudo docker run -it --user $$(id -u):$$(id -g) --rm -v $$(pwd):$$(pwd) -w $$(pwd)
DOCKER_IMAGE := kpouget/qemu-aarch64:stopwatch

all:
	make prepare patch update run-test-quit

prepare:
	./scripts/linux prepare
	./scripts/qemu prepare
	./scripts/rootfs prepare

patch:
	./scripts/patches apply_linux
	./scripts/patches apply_qemu
	./scripts/patches apply_rootfs

update:
	./scripts/linux update
	./scripts/qemu update
	./scripts/rootfs update

run:
	./scripts/run

run-test-quit:
	./scripts/run test-and-quit

module:
	./scripts/linux module

update_patches:
	./scripts/patches create_qemu
	./scripts/patches create_linux
	./scripts/patches create_rootfs

docker:
	${DOCKER_CMD} ${DOCKER_IMAGE} bash

docker-all:
	${DOCKER_CMD} ${DOCKER_IMAGE} make all
