/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_PC_MPTABLE_H
#define MODVM_PC_MPTABLE_H

struct vm_ctx;
struct pci_bus;

#define PC_MAX_VCPUS 254

int pc_build_mptable(struct vm_ctx *ctx, struct pci_bus *bus);

#endif /* MODVM_PC_MPTABLE_H */
