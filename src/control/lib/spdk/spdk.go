//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package spdk provides Go bindings for SPDK
package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo CFLAGS: -I .
#cgo LDFLAGS: -L . -lnvme_control
#cgo LDFLAGS: -lspdk_log -lspdk_env_dpdk -lspdk_nvme -lspdk_vmd -lrte_mempool
#cgo LDFLAGS: -lrte_mempool_ring -lrte_bus_pci

#include "stdlib.h"
#include "daos_srv/control.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/log.h"
#include "spdk/env.h"
#include "spdk/nvme.h"
#include "spdk/vmd.h"
#include "include/nvme_control.h"
#include "include/nvme_control_common.h"

static char **makeCStringArray(int size) {
        return calloc(sizeof(char*), size);
}

static void setArrayString(char **a, char *s, int n) {
        a[n] = s;
}

static void freeCStringArray(char **a, int size) {
        int i;
        for (i = 0; i < size; i++)
                free(a[i]);
        free(a);
}
*/
import "C"

import (
	"fmt"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const vmdDomainLen = 6

// Env is the interface that provides SPDK environment management.
type Env interface {
	InitSPDKEnv(logging.Logger, *EnvOptions) error
	FiniSPDKEnv(logging.Logger, *EnvOptions)
}

// EnvImpl is a an implementation of the Env interface.
type EnvImpl struct{}

// Rc2err returns error from label and rc.
func Rc2err(label string, rc C.int) error {
	return fmt.Errorf("%s: %d", label, rc)
}

// backingAddress2VMD converts VMD backing device PCI addresses (with the VMD
// address encoded in the domain component of the PCI address) back to the PCI
// address of the VMD e.g. [5d0505:01:00.0, 5d0505:03:00.0] -> [0000:5d:05.5].
//
// Many assumptions are made as to the input and output PCI address structure in
// the conversion.
func backingAddress2VMD(log logging.Logger, pciAddrs *common.PCIAddressList) (*common.PCIAddressList, error) {
	if pciAddrs == nil {
		return nil, errors.New("revertBackingToVMD: nil pci address list")
	}

	outAddrs := common.PCIAddressList{}

	for _, inAddr := range *pciAddrs {
		if inAddr.Domain == "0000" {
			outAddrs.Add(*inAddr)
			continue
		}

		// assume non-zero pci address domain field indicates a vmd backing device with
		// fixed length field
		if len(inAddr.Domain) != vmdDomainLen {
			return nil, errors.New("unexpected length of vmd domain")
		}

		vmdAddr := fmt.Sprintf("0000:%s:%s.%s", inAddr.Domain[:2], inAddr.Domain[2:4],
			inAddr.Domain[5:])
		if err := outAddrs.AddStrings(vmdAddr); err != nil {
			log.Debugf("replacing backing device %s with vmd %s", inAddr, vmdAddr)
			return nil, err
		}
	}

	return &outAddrs, nil
}

// EnvOptions describe parameters to be used when initializing a processes
// SPDK environment.
type EnvOptions struct {
	PCIAllowList *common.PCIAddressList // restrict SPDK device access
	EnableVMD    bool                   // flag if VMD functionality should be enabled
}

func (o *EnvOptions) sanitizeAllowList(log logging.Logger) error {
	if o.EnableVMD {
		// DPDK will not accept VMD backing device addresses
		// so convert to VMD addresses
		newAllowList, err := backingAddress2VMD(log, o.PCIAllowList)
		if err != nil {
			return err
		}
		o.PCIAllowList = newAllowList
	}

	return nil
}

// InitSPDKEnv initializes the SPDK environment.
//
// SPDK relies on an abstraction around the local environment
// named env that handles memory allocation and PCI device operations.
// The library must be initialized first.
func (e *EnvImpl) InitSPDKEnv(log logging.Logger, opts *EnvOptions) error {
	log.Debugf("spdk init go opts: %+v", opts)

	// Only print error and more severe to stderr.
	C.spdk_log_set_print_level(C.SPDK_LOG_ERROR)

	if err := opts.sanitizeAllowList(log); err != nil {
		return errors.Wrap(err, "sanitizing PCI include list")
	}

	// Build C array in Go from opts.PCIAllowList []string
	cAllowList := C.makeCStringArray(C.int(opts.PCIAllowList.Len()))
	defer C.freeCStringArray(cAllowList, C.int(opts.PCIAllowList.Len()))

	for i, s := range opts.PCIAllowList.Strings() {
		C.setArrayString(cAllowList, C.CString(s), C.int(i))
	}

	envCtx := C.dpdk_cli_override_opts

	retPtr := C.daos_spdk_init(0, envCtx, C.ulong(opts.PCIAllowList.Len()),
		cAllowList)
	if err := checkRet(retPtr, "daos_spdk_init()"); err != nil {
		return err
	}
	clean(retPtr)

	if opts.EnableVMD {
		if rc := C.spdk_vmd_init(); rc != 0 {
			return Rc2err("spdk_vmd_init()", rc)
		}
	}

	return nil
}

// FiniSPDKEnv initializes the SPDK environment.
func (e *EnvImpl) FiniSPDKEnv(log logging.Logger, opts *EnvOptions) {
	log.Debugf("spdk fini go opts: %+v", opts)

	if opts.EnableVMD {
		C.spdk_vmd_fini()
	}

	C.spdk_env_fini()
}
