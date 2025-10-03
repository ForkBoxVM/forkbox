/* $Id: APICAllCommon.cpp.h 111223 2025-10-03 09:17:51Z ramshankar.venkataraman@oracle.com $ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller - All-context and R3-context common code.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


#ifndef VMM_INCLUDED_SRC_VMMAll_APICAllCommon_cpp_h
#define VMM_INCLUDED_SRC_VMMAll_APICAllCommon_cpp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if !defined(VMM_APIC_TEMPLATE_ALL_COMMON) && !defined(VMM_APIC_TEMPLATE_R3_COMMON)
# error "You must specify APIC all-context and/or APIC R3-context common code to include."
#endif


# ifdef VMM_APIC_TEMPLATE_ALL_COMMON
/**
 * @copydoc{PDMAPICBACKENDR3::pfnInitIpi}
 */
static void apicCommonInitIpi(PVMCPUCC pVCpu)
{
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);

    /*
     * See Intel spec. 10.4.7.3 "Local APIC State After an INIT Reset (Wait-for-SIPI State)"
     * and AMD spec 16.3.2 "APIC Registers".
     *
     * The reason we don't simply zero out the entire APIC page and only set the non-zero members
     * is because there are some registers that are not touched by the INIT IPI (e.g. version)
     * operation and this function is only a subset of the reset operation.
     */
    RT_ZERO(pXApicPage->irr);
    RT_ZERO(pXApicPage->irr);
    RT_ZERO(pXApicPage->isr);
    RT_ZERO(pXApicPage->tmr);
    RT_ZERO(pXApicPage->icr_hi);
    RT_ZERO(pXApicPage->icr_lo);
    RT_ZERO(pXApicPage->ldr);
    RT_ZERO(pXApicPage->tpr);
    RT_ZERO(pXApicPage->ppr);
    RT_ZERO(pXApicPage->timer_icr);
    RT_ZERO(pXApicPage->timer_ccr);
    RT_ZERO(pXApicPage->timer_dcr);

    pXApicPage->dfr.u.u4Model        = XAPICDESTFORMAT_FLAT;
    pXApicPage->dfr.u.u28ReservedMb1 = UINT32_C(0xfffffff);

    /** @todo CMCI. */

    RT_ZERO(pXApicPage->lvt_timer);
    pXApicPage->lvt_timer.u.u1Mask = 1;

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    RT_ZERO(pXApicPage->lvt_thermal);
    pXApicPage->lvt_thermal.u.u1Mask = 1;
#endif

    RT_ZERO(pXApicPage->lvt_perf);
    pXApicPage->lvt_perf.u.u1Mask = 1;

    RT_ZERO(pXApicPage->lvt_lint0);
    pXApicPage->lvt_lint0.u.u1Mask = 1;

    RT_ZERO(pXApicPage->lvt_lint1);
    pXApicPage->lvt_lint1.u.u1Mask = 1;

    RT_ZERO(pXApicPage->lvt_error);
    pXApicPage->lvt_error.u.u1Mask = 1;

    RT_ZERO(pXApicPage->svr);
    pXApicPage->svr.u.u8SpuriousVector = 0xff;

    /* The self-IPI register is reset to 0. See Intel spec. 10.12.5.1 "x2APIC States" */
    PX2APICPAGE pX2ApicPage = VMCPU_TO_X2APICPAGE(pVCpu);
    RT_ZERO(pX2ApicPage->self_ipi);
}


/**
 * Checks if this APIC belongs to a logical destination.
 *
 * @returns @c true if the APIC belongs to the logical destination,
 *          @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fDest   The destination mask.
 *
 * @thread  Any.
 */
static bool apicCommonIsLogicalDest(PVMCPUCC pVCpu, uint32_t fDest)
{
    if (XAPIC_IN_X2APIC_MODE(pVCpu->apic.s.uApicBaseMsr))
    {
        /*
         * Flat logical mode is not supported in x2APIC mode.
         * In clustered logical mode, the 32-bit logical ID in the LDR is interpreted as follows:
         *    - High 16 bits is the cluster ID.
         *    - Low 16 bits: each bit represents a unique APIC within the cluster.
         */
        PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpu);
        uint32_t const u32Ldr    = pX2ApicPage->ldr.u32LogicalApicId;
        if (X2APIC_LDR_GET_CLUSTER_ID(u32Ldr) == (fDest & X2APIC_LDR_CLUSTER_ID))
            return RT_BOOL(u32Ldr & fDest & X2APIC_LDR_LOGICAL_ID);
        return false;
    }

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    /*
     * In both flat and clustered logical mode, a destination mask of all set bits indicates a broadcast.
     * See AMD spec. 16.6.1 "Receiving System and IPI Interrupts".
     */
    Assert(!XAPIC_IN_X2APIC_MODE(pVCpu->apic.s.uApicBaseMsr));
    if ((fDest & XAPIC_LDR_FLAT_LOGICAL_ID) == XAPIC_LDR_FLAT_LOGICAL_ID)
        return true;

    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    XAPICDESTFORMAT enmDestFormat = (XAPICDESTFORMAT)pXApicPage->dfr.u.u4Model;
    if (enmDestFormat == XAPICDESTFORMAT_FLAT)
    {
        /* The destination mask is interpreted as a bitmap of 8 unique logical APIC IDs. */
        uint8_t const u8Ldr = pXApicPage->ldr.u.u8LogicalApicId;
        return RT_BOOL(u8Ldr & fDest & XAPIC_LDR_FLAT_LOGICAL_ID);
    }

    /*
     * In clustered logical mode, the 8-bit logical ID in the LDR is interpreted as follows:
     *    - High 4 bits is the cluster ID.
     *    - Low 4 bits: each bit represents a unique APIC within the cluster.
     */
    Assert(enmDestFormat == XAPICDESTFORMAT_CLUSTER);
    uint8_t const u8Ldr = pXApicPage->ldr.u.u8LogicalApicId;
    if (XAPIC_LDR_CLUSTERED_GET_CLUSTER_ID(u8Ldr) == (fDest & XAPIC_LDR_CLUSTERED_CLUSTER_ID))
        return RT_BOOL(u8Ldr & fDest & XAPIC_LDR_CLUSTERED_LOGICAL_ID);
    return false;
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
}


/**
 * Figures out the set of destination CPUs for a given destination mode, format
 * and delivery mode setting.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   fDestMask           The destination mask.
 * @param   fBroadcastMask      The broadcast mask.
 * @param   enmDestMode         The destination mode.
 * @param   enmDeliveryMode     The delivery mode.
 * @param   pDestCpuSet         The destination CPU set to update.
 */
static void apicCommonGetDestCpuSet(PVMCC pVM, uint32_t fDestMask, uint32_t fBroadcastMask, XAPICDESTMODE enmDestMode,
                                    XAPICDELIVERYMODE enmDeliveryMode, PVMCPUSET pDestCpuSet)
{
    VMCPUSET_EMPTY(pDestCpuSet);

    /*
     * Physical destination mode only supports either a broadcast or a single target.
     *    - Broadcast with lowest-priority delivery mode is not supported[1], we deliver it
     *      as a regular broadcast like in fixed delivery mode.
     *    - For a single target, lowest-priority delivery mode makes no sense. We deliver
     *      to the target like in fixed delivery mode.
     *
     * [1] See Intel spec. 10.6.2.1 "Physical Destination Mode".
     */
    if (   enmDestMode == XAPICDESTMODE_PHYSICAL
        && enmDeliveryMode == XAPICDELIVERYMODE_LOWEST_PRIO)
    {
        AssertMsgFailed(("APIC: Lowest-priority delivery using physical destination mode!"));
        enmDeliveryMode = XAPICDELIVERYMODE_FIXED;
    }

    uint32_t const cCpus = pVM->cCpus;
    if (enmDeliveryMode == XAPICDELIVERYMODE_LOWEST_PRIO)
    {
        Assert(enmDestMode == XAPICDESTMODE_LOGICAL);
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
        VMCPUID idCpuLowestTpr = NIL_VMCPUID;
        uint8_t u8LowestTpr    = UINT8_C(0xff);
        for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        {
            PVMCPUCC pVCpuDst = pVM->CTX_SUFF(apCpus)[idCpu];
            if (apicCommonIsLogicalDest(pVCpuDst, fDestMask))
            {
                PCXAPICPAGE   pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpuDst);
                uint8_t const u8Tpr      = pXApicPage->tpr.u8Tpr;         /* PAV */

                /*
                 * If there is a tie for lowest priority, the local APIC with the highest ID is chosen.
                 * Hence the use of "<=" in the check below.
                 * See AMD spec. 16.6.2 "Lowest Priority Messages and Arbitration".
                 */
                if (u8Tpr <= u8LowestTpr)
                {
                    u8LowestTpr    = u8Tpr;
                    idCpuLowestTpr = idCpu;
                }
            }
        }
        if (idCpuLowestTpr != NIL_VMCPUID)
            VMCPUSET_ADD(pDestCpuSet, idCpuLowestTpr);
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
        return;
    }

    /*
     * x2APIC:
     *    - In both physical and logical destination mode, a destination mask of 0xffffffff implies a broadcast[1].
     * xAPIC:
     *    - In physical destination mode, a destination mask of 0xff implies a broadcast[2].
     *    - In both flat and clustered logical mode, a destination mask of 0xff implies a broadcast[3].
     *
     * [1] See Intel spec. 10.12.9 "ICR Operation in x2APIC Mode".
     * [2] See Intel spec. 10.6.2.1 "Physical Destination Mode".
     * [2] See AMD spec. 16.6.1 "Receiving System and IPI Interrupts".
     */
    if ((fDestMask & fBroadcastMask) == fBroadcastMask)
    {
        VMCPUSET_FILL(pDestCpuSet);
        return;
    }

    if (enmDestMode == XAPICDESTMODE_PHYSICAL)
    {
        /* The destination mask is interpreted as the physical APIC ID of a single target. */
#if 1
        /* Since our physical APIC ID is read-only to software, set the corresponding bit in the CPU set. */
        if (RT_LIKELY(fDestMask < cCpus))
            VMCPUSET_ADD(pDestCpuSet, fDestMask);
#else
        /* The physical APIC ID may not match our VCPU ID, search through the list of targets. */
        for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        {
            PVMCPUCC pVCpuDst = &pVM->aCpus[idCpu];
            if (XAPIC_IN_X2APIC_MODE(pVCpuDst))
            {
                PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpuDst);
                if (pX2ApicPage->id.u32ApicId == fDestMask)
                    VMCPUSET_ADD(pDestCpuSet, pVCpuDst->idCpu);
            }
            else
            {
                PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpuDst);
                if (pXApicPage->id.u8ApicId == (uint8_t)fDestMask)
                    VMCPUSET_ADD(pDestCpuSet, pVCpuDst->idCpu);
            }
        }
#endif
    }
    else
    {
        Assert(enmDestMode == XAPICDESTMODE_LOGICAL);

        /* A destination mask of all 0's implies no target APICs (since it's interpreted as a bitmap or partial bitmap). */
        if (RT_UNLIKELY(!fDestMask))
            return;

        /* The destination mask is interpreted as a bitmap of software-programmable logical APIC ID of the target APICs. */
        for (VMCPUID idCpu = 0; idCpu < cCpus; idCpu++)
        {
            PVMCPUCC pVCpuDst = pVM->CTX_SUFF(apCpus)[idCpu];
            if (apicCommonIsLogicalDest(pVCpuDst, fDestMask))
                VMCPUSET_ADD(pDestCpuSet, pVCpuDst->idCpu);
        }
    }
}


/**
 * Resets the APIC base MSR.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void apicCommonResetBaseMsr(PVMCPUCC pVCpu)
{
    /*
     * Initialize the APIC base MSR. The APIC enable-bit is set upon power-up or reset[1].
     *
     * A Reset (in xAPIC and x2APIC mode) brings up the local APIC in xAPIC mode.
     * An INIT IPI does -not- cause a transition between xAPIC and x2APIC mode[2].
     *
     * [1] See AMD spec. 14.1.3 "Processor Initialization State"
     * [2] See Intel spec. 10.12.5.1 "x2APIC States".
     */
    uint64_t uApicBaseMsr = MSR_IA32_APICBASE_ADDR;
    if (pVCpu->idCpu == 0)
        uApicBaseMsr |= MSR_IA32_APICBASE_BSP;

    /* If the VM was configured with no APIC, don't enable xAPIC mode, obviously. */
    if (pVCpu->CTX_SUFF(pVM)->apic.s.enmMaxMode != PDMAPICMODE_NONE)
    {
        uApicBaseMsr |= MSR_IA32_APICBASE_EN;

        /*
         * While coming out of a reset the APIC is enabled and in xAPIC mode. If software had previously
         * disabled the APIC (which results in the CPUID bit being cleared as well) we re-enable it here.
         * See Intel spec. 10.12.5.1 "x2APIC States".
         */
        if (CPUMSetGuestCpuIdPerCpuApicFeature(pVCpu, true /*fVisible*/) == false)
            LogRel(("APIC%u: Resetting mode to xAPIC\n", pVCpu->idCpu));
    }

    /* Commit. */
    ASMAtomicWriteU64(&pVCpu->apic.s.uApicBaseMsr, uApicBaseMsr);
}
# endif /* VMM_APIC_TEMPLATE_ALL_COMMON */


# if defined(IN_RING3) && defined(VMM_APIC_TEMPLATE_R3_COMMON)
/**
 * Sets the CPUID feature bits for the APIC mode.
 *
 * @param   pVM             The cross context VM structure.
 * @param   enmMode         The APIC mode.
 */
static void apicR3CommonSetCpuIdFeatureLevel(PVM pVM, PDMAPICMODE enmMode)
{
    /** @todo Merge with apicR3HvSetCpuIdFeatureLevel. */
    switch (enmMode)
    {
        case PDMAPICMODE_NONE:
            CPUMR3ClearGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_X2APIC);
            CPUMR3ClearGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_APIC);
            break;

        case PDMAPICMODE_APIC:
            CPUMR3ClearGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_X2APIC);
            CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_APIC);
            break;

        case PDMAPICMODE_X2APIC:
            CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_APIC);
            CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_X2APIC);
            break;

        default:
            AssertMsgFailed(("Unknown/invalid APIC mode: %d\n", (int)enmMode));
    }
}
# endif /* IN_RING3 && VMM_APIC_TEMPLATE_R3_COMMON */

#endif /* VMM_INCLUDED_SRC_VMMAll_APICAllCommon_cpp_h */

