; Mac OS X 10.5 (Leopard) support: the two vtable slots Tiger's headers still
; call reserved and Leopard made real methods.
;
; Every IOService subclass built with Tiger's headers has a vtable entry that
; points at IOService::_RESERVEDIOService4 (and an IOUserClient subclass one
; at IOUserClient::_RESERVEDIOUserClient0). Leopard's kernel has no such
; functions any more: those slots are IOService::systemWillShutdown and
; IOUserClient::externalMethod, so the kext would not link ("undefined
; symbol"). The two stubs below define the old names and jump to the
; methods that now sit in the same slots, with the arguments untouched, so
; the vtable behaves as if the kext had been built with Leopard's headers.
; Built only by `make LEOPARD=1`; on Tiger the targets do not exist.
;
; The jumps go through CTR with a full 32-bit address: the kernel is far
; from the kext, out of range of a direct branch.
;
; Copyright (c) 2026 kouta-kun and Claude
; SPDX-License-Identifier: MIT

	.text
	.align 2

	.globl __ZN9IOService19_RESERVEDIOService4Ev
__ZN9IOService19_RESERVEDIOService4Ev:
	lis	r12,hi16(__ZN9IOService18systemWillShutdownEm)
	ori	r12,r12,lo16(__ZN9IOService18systemWillShutdownEm)
	mtctr	r12
	bctr

	.globl __ZN12IOUserClient22_RESERVEDIOUserClient0Ev
__ZN12IOUserClient22_RESERVEDIOUserClient0Ev:
	lis	r12,hi16(__ZN12IOUserClient14externalMethodEjP25IOExternalMethodArgumentsP24IOExternalMethodDispatchP8OSObjectPv)
	ori	r12,r12,lo16(__ZN12IOUserClient14externalMethodEjP25IOExternalMethodArgumentsP24IOExternalMethodDispatchP8OSObjectPv)
	mtctr	r12
	bctr
