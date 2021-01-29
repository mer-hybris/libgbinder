# -*- Mode: makefile-gmake -*-

all:
%:
	@$(MAKE) -C unit_bridge $*
	@$(MAKE) -C unit_buffer $*
	@$(MAKE) -C unit_cleanup $*
	@$(MAKE) -C unit_client $*
	@$(MAKE) -C unit_config $*
	@$(MAKE) -C unit_driver $*
	@$(MAKE) -C unit_eventloop $*
	@$(MAKE) -C unit_ipc $*
	@$(MAKE) -C unit_local_object $*
	@$(MAKE) -C unit_local_reply $*
	@$(MAKE) -C unit_local_request $*
	@$(MAKE) -C unit_log $*
	@$(MAKE) -C unit_protocol $*
	@$(MAKE) -C unit_proxy_object $*
	@$(MAKE) -C unit_reader $*
	@$(MAKE) -C unit_remote_object $*
	@$(MAKE) -C unit_remote_reply $*
	@$(MAKE) -C unit_remote_request $*
	@$(MAKE) -C unit_servicemanager $*
	@$(MAKE) -C unit_servicemanager_aidl $*
	@$(MAKE) -C unit_servicemanager_aidl2 $*
	@$(MAKE) -C unit_servicemanager_hidl $*
	@$(MAKE) -C unit_servicename $*
	@$(MAKE) -C unit_servicepoll $*
	@$(MAKE) -C unit_writer $*

clean: unitclean
	rm -f coverage/*.gcov
	rm -fr coverage/report
