//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

func TestPCIUtils_NewPCIAddress(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStr string
		expDom  string
		expBus  string
		expDev  string
		expFun  string
		expErr  error
	}{
		"valid": {
			addrStr: "0000:80:00.0",
			expDom:  "0000",
			expBus:  "80",
			expDev:  "00",
			expFun:  "0",
		},
		"invalid": {
			addrStr: "0000:gg:00.0",
			expErr:  errors.New("parsing \"gg\""),
		},
		"vmd address": {
			addrStr: "0000:5d:05.5",
			expDom:  "0000",
			expBus:  "5d",
			expDev:  "05",
			expFun:  "5",
		},
		"vmd backing device address": {
			addrStr: "5d0505:01:00.0",
			expDom:  "5d0505",
			expBus:  "01",
			expDev:  "00",
			expFun:  "0",
		},
	} {
		t.Run(name, func(t *testing.T) {
			addr, err := NewPCIAddress(tc.addrStr)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expDom, addr.Domain, "bad domain")
			AssertEqual(t, tc.expBus, addr.Bus, "bad bus")
			AssertEqual(t, tc.expDev, addr.Device, "bad device")
			AssertEqual(t, tc.expFun, addr.Function, "bad function")
		})
	}
}

func TestPCIUtils_NewPCIAddressList(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStrs    []string
		expAddrStr  string
		expAddrStrs []string
		expErr      error
	}{
		"valid": {
			addrStrs: []string{
				"0000:7f:00.1", "0000:81:00.0", "0000:7f:01.0", "0000:80:00.0",
				"0000:7f:00.0", "0000:7e:00.0", "5d0505:01:00.0",
			},
			expAddrStr: "0000:7e:00.0 0000:7f:00.0 0000:7f:00.1 0000:7f:01.0 " +
				"0000:80:00.0 0000:81:00.0 5d0505:01:00.0",
			expAddrStrs: []string{
				"0000:7e:00.0", "0000:7f:00.0", "0000:7f:00.1", "0000:7f:01.0",
				"0000:80:00.0", "0000:81:00.0", "5d0505:01:00.0",
			},
		},
		"invalid": {
			addrStrs: []string{"0000:7f.00.0"},
			expErr:   errors.New("bdf format"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrList, err := NewPCIAddressList(tc.addrStrs...)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expAddrStr, addrList.String()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expAddrStrs, addrList.Strings()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPCIUtils_PCIAddressList_Intersect(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStrs          []string
		intersectAddrStrs []string
		expAddrStrs       []string
	}{
		"partial": {
			addrStrs: []string{
				"0000:7f:00.1", "0000:81:00.0", "0000:7f:01.0", "0000:80:00.0",
				"0000:7f:00.0", "0000:7e:00.0",
			},
			intersectAddrStrs: []string{
				"0000:7e:00.0", "0000:7f:00.0", "0000:7f:00.1", "0000:7f:01.1",
			},
			expAddrStrs: []string{
				"0000:7e:00.0", "0000:7f:00.0", "0000:7f:00.1",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrList, err := NewPCIAddressList(tc.addrStrs...)
			if err != nil {
				t.Fatal(err)
			}

			intersectAddrList, err := NewPCIAddressList(tc.intersectAddrStrs...)
			if err != nil {
				t.Fatal(err)
			}

			intersection := addrList.Intersect(intersectAddrList)

			if diff := cmp.Diff(tc.expAddrStrs, intersection.Strings()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPCIUtils_PCIAddressList_Difference(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStrs           []string
		differenceAddrStrs []string
		expAddrStrs        []string
	}{
		"partial": {
			addrStrs: []string{
				"0000:7f:00.1", "0000:81:00.0", "0000:7f:01.0", "0000:80:00.0",
				"0000:7f:00.0", "0000:7e:00.0",
			},
			differenceAddrStrs: []string{
				"0000:7e:00.0", "0000:7f:00.0", "0000:7f:00.1", "0000:7f:01.1",
			},
			expAddrStrs: []string{
				"0000:7f:01.0", "0000:80:00.0", "0000:81:00.0",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrList, err := NewPCIAddressList(tc.addrStrs...)
			if err != nil {
				t.Fatal(err)
			}

			differenceAddrList, err := NewPCIAddressList(tc.differenceAddrStrs...)
			if err != nil {
				t.Fatal(err)
			}

			difference := addrList.Difference(differenceAddrList)

			if diff := cmp.Diff(tc.expAddrStrs, difference.Strings()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}
