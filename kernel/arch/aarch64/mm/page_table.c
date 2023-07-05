/*
 * Copyright (c) 2022 Institute of Parallel And Distributed Systems (IPADS)
 * ChCore-Lab is licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan
 * PSL v1. You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE. See the
 * Mulan PSL v1 for more details.
 */

#include <common/util.h>
#include <common/vars.h>
#include <common/macro.h>
#include <common/types.h>
#include <common/errno.h>
#include <lib/printk.h>
#include <mm/kmalloc.h>
#include <mm/mm.h>
#include <arch/mmu.h>

#include <arch/mm/page_table.h>

#define KERNEL_OFFSET 0xFFFFFF0000000000

extern void set_ttbr0_el1(paddr_t);

void set_page_table(paddr_t pgtbl)
{
        set_ttbr0_el1(pgtbl);
}

#define USER_PTE 0
/*
 * the 3rd arg means the kind of PTE.
 */
static int set_pte_flags(pte_t *entry, vmr_prop_t flags, int kind)
{
        // Only consider USER PTE now.
        BUG_ON(kind != USER_PTE);

        /*
         * Current access permission (AP) setting:
         * Mapped pages are always readable (No considering XOM).
         * EL1 can directly access EL0 (No restriction like SMAP
         * as ChCore is a microkernel).
         */
        if (flags & VMR_WRITE)
                entry->l3_page.AP = AARCH64_MMU_ATTR_PAGE_AP_HIGH_RW_EL0_RW;
        else
                entry->l3_page.AP = AARCH64_MMU_ATTR_PAGE_AP_HIGH_RO_EL0_RO;

        if (flags & VMR_EXEC)
                entry->l3_page.UXN = AARCH64_MMU_ATTR_PAGE_UX;
        else
                entry->l3_page.UXN = AARCH64_MMU_ATTR_PAGE_UXN;

        // EL1 cannot directly execute EL0 accessiable region.
        entry->l3_page.PXN = AARCH64_MMU_ATTR_PAGE_PXN;
        // Set AF (access flag) in advance.
        entry->l3_page.AF = AARCH64_MMU_ATTR_PAGE_AF_ACCESSED;
        // Mark the mapping as not global
        entry->l3_page.nG = 1;
        // Mark the mappint as inner sharable
        entry->l3_page.SH = INNER_SHAREABLE;
        // Set the memory type
        if (flags & VMR_DEVICE) {
                entry->l3_page.attr_index = DEVICE_MEMORY;
                entry->l3_page.SH = 0;
        } else if (flags & VMR_NOCACHE) {
                entry->l3_page.attr_index = NORMAL_MEMORY_NOCACHE;
        } else {
                entry->l3_page.attr_index = NORMAL_MEMORY;
        }

        return 0;
}

/* PTP<-->Page Table Page */
#define GET_PADDR_IN_PTE(entry) \
        (((u64)entry->table.next_table_addr) << PAGE_SHIFT)
#define GET_NEXT_PTP(entry) phys_to_virt(GET_PADDR_IN_PTE(entry))

#define NORMAL_PTP (0)
#define BLOCK_PTP  (1)

/*
 * Find next page table page for the "va".
 *
 * cur_ptp: current page table page
 * level:   current ptp level
 *
 * next_ptp: returns "next_ptp"
 * pte     : returns "pte" (points to next_ptp) in "cur_ptp"
 *
 * alloc: if true, allocate a ptp when missing
 *
 */
static int get_next_ptp(ptp_t *cur_ptp, u32 level, vaddr_t va, ptp_t **next_ptp,
                        pte_t **pte, bool alloc)
{
        u32 index = 0;
        pte_t *entry;

        if (cur_ptp == NULL)
                return -ENOMAPPING;

        switch (level) {
        case 0:
                index = GET_L0_INDEX(va);
                break;
        case 1:
                index = GET_L1_INDEX(va);
                break;
        case 2:
                index = GET_L2_INDEX(va);
                break;
        case 3:
                index = GET_L3_INDEX(va);
                break;
        default:
                BUG_ON(1);
        }

        entry = &(cur_ptp->ent[index]);
        if (IS_PTE_INVALID(entry->pte)) {
                if (alloc == false) {
                        return -ENOMAPPING;
                } else {
                        /* alloc a new page table page */
                        ptp_t *new_ptp;
                        paddr_t new_ptp_paddr;
                        pte_t new_pte_val;

                        // TODO: finish this function.
                        /* alloc a single physical page as a new page table page  */
                        /* LAB 2 TODO 3 BEGIN 
                         * Hint: use get_pages to allocate a new page table page
                         *       set the attr `is_valid`, `is_table` and `next_table_addr` of new pte
                         */
                        int order = 0; // try to allocate a new page start from order 0.i.e., only allocate ONE new page.
                        // allocate ONE new page here.
                        new_ptp = get_pages(order);
                        // clear everything after a new page is allocated.
                        memset((void*) new_ptp, 0, PAGE_SIZE);
                        // a newly-allocated page will have a VM, convert it to a PM here.
                        new_ptp_paddr = virt_to_phys((vaddr_t) new_ptp);
                        new_pte_val.pte = 0;
                        new_pte_val.table.is_table = 1; // set is_table
                        new_pte_val.table.is_valid = 1; // set is_valid
                        // unsure here.
                        // As the next_table_addr in the union takes 36 bits, so we should right-shift the real-address 12 bit to get it.
                        new_pte_val.table.next_table_addr = new_ptp_paddr >> 12; // set next_table_addr
                        entry->pte = new_pte_val.pte;
                        /* LAB 2 TODO 3 END */
                }
        }

        *next_ptp = (ptp_t *)GET_NEXT_PTP(entry);
        *pte = entry;
        if (IS_PTE_TABLE(entry->pte))
                return NORMAL_PTP;
        else
                return BLOCK_PTP;
}

void free_page_table(void *pgtbl)
{
        ptp_t *l0_ptp, *l1_ptp, *l2_ptp, *l3_ptp;
        pte_t *l0_pte, *l1_pte, *l2_pte;
        int i, j, k;

        if (pgtbl == NULL) {
                kwarn("%s: input arg is NULL.\n", __func__);
                return;
        }

        /* L0 page table */
        l0_ptp = (ptp_t *)pgtbl;

        /* Interate each entry in the l0 page table*/
        for (i = 0; i < PTP_ENTRIES; ++i) {
                l0_pte = &l0_ptp->ent[i];
                if (IS_PTE_INVALID(l0_pte->pte) || !IS_PTE_TABLE(l0_pte->pte))
                        continue;
                l1_ptp = (ptp_t *)GET_NEXT_PTP(l0_pte);

                /* Interate each entry in the l1 page table*/
                for (j = 0; j < PTP_ENTRIES; ++j) {
                        l1_pte = &l1_ptp->ent[j];
                        if (IS_PTE_INVALID(l1_pte->pte)
                            || !IS_PTE_TABLE(l1_pte->pte))
                                continue;
                        l2_ptp = (ptp_t *)GET_NEXT_PTP(l1_pte);

                        /* Interate each entry in the l2 page table*/
                        for (k = 0; k < PTP_ENTRIES; ++k) {
                                l2_pte = &l2_ptp->ent[k];
                                if (IS_PTE_INVALID(l2_pte->pte)
                                    || !IS_PTE_TABLE(l2_pte->pte))
                                        continue;
                                l3_ptp = (ptp_t *)GET_NEXT_PTP(l2_pte);
                                /* Free the l3 page table page */
                                free_pages(l3_ptp);
                        }

                        /* Free the l2 page table page */
                        free_pages(l2_ptp);
                }

                /* Free the l1 page table page */
                free_pages(l1_ptp);
        }

        free_pages(l0_ptp);
}

/*
 * Translate a va to pa, and get its pte for the flags
 */
int query_in_pgtbl(void *pgtbl, vaddr_t va, paddr_t *pa, pte_t **entry)
{
        /* LAB 2 TODO 3 BEGIN */
        /*
         * Hint: Walk through each level of page table using `get_next_ptp`,
         * return the pa and pte until a L0/L1 block or page, return
         * `-ENOMAPPING` if the va is not mapped.
         */
        ptp_t* cur_ptp = (ptp_t*) pgtbl;
        ptp_t* next_ptp_0;
        ptp_t* next_ptp_1;
        ptp_t* next_ptp_2;
        ptp_t* next_ptp_3;
        pte_t* pte;
        // GET_LX_INDEX is used to get 
        // query in L0 first.
        int res0 = get_next_ptp(cur_ptp, 0, va, &next_ptp_0, &pte, false);
        if (res0 == -ENOMAPPING) return -ENOMAPPING;
        // query in L1.
        int res1 = get_next_ptp(next_ptp_0, 1, va, &next_ptp_1, &pte, false);
        if (res1 == -ENOMAPPING) return -ENOMAPPING;
        if (res1 == BLOCK_PTP) {
                // use the huge-page of size-1GB.
                if (entry != NULL) *entry = pte;
                paddr_t res1_addr = virt_to_phys(next_ptp_1) + GET_VA_OFFSET_L1(va);
                *pa = res1_addr;
                return 0;
        }
        // query in L2.
        int res2 = get_next_ptp(next_ptp_1, 2, va, &next_ptp_2, &pte, false);
        if (res2 == -ENOMAPPING) return -ENOMAPPING;
        if (res2 == BLOCK_PTP) {
                // use the huge-page of size-2MB.
                if (entry != NULL) *entry = pte;
                paddr_t res2_addr = virt_to_phys(next_ptp_2) + GET_VA_OFFSET_L2(va);
                *pa = res2_addr;
                return 0;
        }
        // query in L3.
        int res3 = get_next_ptp(next_ptp_2, 3, va, &next_ptp_3, &pte, false);
        if (res3 == -ENOMAPPING) return -ENOMAPPING;
        // the L3 level table have NO Block-ptp, only Page.
        if (entry != NULL) *entry = pte;
        *pa = virt_to_phys(next_ptp_3) + GET_VA_OFFSET_L3(va);
        return 0;
        /* LAB 2 TODO 3 END */
}

// used to ADD new mapping relations.
int map_range_in_pgtbl(void *pgtbl, vaddr_t va, paddr_t pa, size_t len,
                       vmr_prop_t flags)
{
        /* LAB 2 TODO 3 BEGIN */
        /*
         * Hint: Walk through each level of page table using `get_next_ptp`,
         * create new page table page if necessary, fill in the final level
         * pte with the help of `set_pte_flags`. Iterate until all pages are
         * mapped.
         */
        size_t num = 0;
        if (len % PAGE_SIZE == 0) num = len / PAGE_SIZE;
        else num = len / PAGE_SIZE + 1;
        for (size_t i = 0; i < num; ++i) {
                ptp_t* cur_ptp = (ptp_t*) pgtbl;
                ptp_t* next_ptp_0;
                ptp_t* next_ptp_1;
                ptp_t* next_ptp_2;
                ptp_t* next_ptp_3;
                pte_t* pte;
                // as it is a MAP function, so we need to map need mapping when there is no.
                int res0 = get_next_ptp(cur_ptp, 0, va, &cur_ptp, &pte, true);
                // if (res0 == -ENOMAPPING) return res0;
                int res1 = get_next_ptp(cur_ptp, 1, va, &cur_ptp, &pte, true);
                // if (res1 == -ENOMAPPING) return res1;
                int res2 = get_next_ptp(cur_ptp, 2, va, &cur_ptp, &pte, true);
                // // if (res2 == -ENOMAPPING) return res2;
                // pte_t new_pte;
                // new_pte.pte = 0;
                // new_pte.l3_page.pfn = pa >> 12;
                // new_pte.l3_page.is_page = 1;
                // new_pte.l3_page.is_valid = 1;
                // // set_pte_flags(&new_pte, flags, USER_PTE);
                // if (va & (KERNEL_OFFSET)) {
                //         set_pte_flags(&new_pte, flags, USER_PTE);
                // }
                // else set_pte_flags(&new_pte, flags, USER_PTE);
                // next_ptp_2->ent[GET_L3_INDEX(va)].pte = new_pte.pte;
                pte = &(cur_ptp->ent[GET_L3_INDEX(va)]);
                pte->pte = 0;
                pte->l3_page.pfn = ((pa) >> L3_INDEX_SHIFT);
                pte->l3_page.is_page = 1;
                pte->l3_page.is_valid = 1;
                if (va & (KERNEL_OFFSET)) {
                        set_pte_flags(pte, flags, USER_PTE);
                } else {
                        set_pte_flags(pte, flags, USER_PTE);
                }
                va += PAGE_SIZE;
                pa += PAGE_SIZE;
        }
        return 0;
        /* LAB 2 TODO 3 END */
}

int unmap_range_in_pgtbl(void *pgtbl, vaddr_t va, size_t len)
{
        /* LAB 2 TODO 3 BEGIN */
        /*
         * Hint: Walk through each level of page table using `get_next_ptp`,
         * mark the final level pte as invalid. Iterate until all pages are
         * unmapped.
         */
        // int num = 0;
        // if (len % PAGE_SIZE == 0) num = len / PAGE_SIZE;
        // else num = len / PAGE_SIZE + 1;
        // for (size_t i = 0; i < num; ++i) {
        //         ptp_t* cur_ptp = (ptp_t*) pgtbl;
        //         ptp_t* next_ptp_0;
        //         ptp_t* next_ptp_1;
        //         ptp_t* next_ptp_2;
        //         ptp_t* next_ptp_3;
        //         pte_t* pte;
        //         int res0 = get_next_ptp(cur_ptp, 0, va, &cur_ptp, &pte, false);
        //         // if (res0 == -ENOMAPPING) return res0;
        //         int res1 = get_next_ptp(cur_ptp, 1, va, &cur_ptp, &pte, false);
        //         // if (res1 == -ENOMAPPING) return res1;
        //         int res2 = get_next_ptp(cur_ptp, 2, va, &cur_ptp, &pte, false);
        //         // if (res2 == -ENOMAPPING) return res2;
        //         // int res3 = get_next_ptp(next_ptp_2, 3, va, &next_ptp_3, &pte, false);
        //         // if (res3 == -ENOMAPPING) return res3;
        //         // invalid all final-level pages.
        //         (pte->l3_page).is_valid = 0;
        //         cur_ptp->ent[GET_L3_INDEX(va)].pte = 0;
        //         // pte->pte = (pte->pte & (~(0x1ULL)));
        //         va += PAGE_SIZE;
        // }
        // return 0;

        ptp_t *next_ptp, *current_ptp = (ptp_t *)pgtbl;
        pte_t *pte;
        u64 index;
        // for (current_ptp = (ptp_t *)pgtbl, index = 0; index < len;
        // index += PAGE_SIZE, current_ptp = (ptp_t *)pgtbl) {
        //         for (int level = 0; level < 3; level++, current_ptp = next_ptp)
        //                 get_next_ptp(current_ptp, level, va + index, &next_ptp, &pte, false);
        //         pte = &current_ptp->ent[GET_L3_INDEX((va + index))];
        //         memset(pte, 0, sizeof(pte_t));
        // }
        for (index = 0; index < len; index += PAGE_SIZE) {
                current_ptp = (ptp_t*) pgtbl;
                get_next_ptp(current_ptp, 0, va + index, &current_ptp, &pte, false);
                get_next_ptp(current_ptp, 1, va + index, &current_ptp, &pte, false);
                get_next_ptp(current_ptp, 2, va + index, &current_ptp, &pte, false);
                pte = &current_ptp->ent[GET_L3_INDEX((va + index))];
                memset(pte, 0, sizeof(pte_t));
        }
        return 0;
        /* LAB 2 TODO 3 END */
}

int map_range_in_pgtbl_huge(void *pgtbl, vaddr_t va, paddr_t pa, size_t len,
                            vmr_prop_t flags)
{
        /* LAB 2 TODO 4 BEGIN */
        u64 size1 = PAGE_SIZE * PTP_ENTRIES * PTP_ENTRIES;
        u64 num1 = len / size1;
        // use the biggest (1GB) page to map part of addresses.
        for (int i = 0; i < num1; ++i) {
                ptp_t* cur_ptp = (ptp_t*) pgtbl;
                pte_t* pte;
                int res0 = get_next_ptp(cur_ptp, 0, va, &cur_ptp, &pte, true);
                // if (res0 == -ENOMAPPING) return res0;
                // (pte->table).next_table_addr = pa >> 12;
                // (pte->l1_block).is_valid = 1;
                // // this page should be a BLOCK but not a TABLE.
                // (pte->l1_block).is_table = 0;
                // set_pte_flags(pte, flags, USER_PTE);
                pte_t new_pte;
                new_pte.pte = 0;
                new_pte.l1_block.is_table = 0;
                new_pte.l1_block.is_valid = 1;
                new_pte.l1_block.pfn = pa >> 30;
                set_pte_flags(&new_pte, flags, USER_PTE);
                cur_ptp->ent[GET_L1_INDEX(va)].pte = new_pte.pte;
                va += size1;
                pa += size1;
        }
        len -= num1 * size1;
        // L2 case.
        u64 size2 = PAGE_SIZE * PTP_ENTRIES;
        u64 num2 = len / size2;
        for (int i = 0; i < num2; ++i) {
                ptp_t* cur_ptp = (ptp_t*) pgtbl;
                ptp_t* next_ptp_0;
                ptp_t* next_ptp_1;
                pte_t* pte;
                int res0 = get_next_ptp(cur_ptp, 0, va, &cur_ptp, &pte, true);
                // if (res0 == -ENOMAPPING) return res0;
                int res1 = get_next_ptp(cur_ptp, 1, va, &cur_ptp, &pte, true);
                // if (res1 == -ENOMAPPING) return res1;
                // set_pte_flags(pte, flags, USER_PTE);
                // (pte->table).next_table_addr = pa >> 12;
                // (pte->l2_block).is_valid = 1;
                // // this page should be a BLOCK but not a TABLE.
                // (pte->l2_block).is_table = 0;
                pte_t new_pte;
                new_pte.pte = 0;
                new_pte.l2_block.is_table = 0;
                new_pte.l2_block.is_valid = 1;
                new_pte.l2_block.pfn = pa >> 21;
                set_pte_flags(&new_pte, flags, USER_PTE);
                cur_ptp->ent[GET_L2_INDEX(va)].pte = new_pte.pte;
                va += size2;
                pa += size2;
        }
        // L3 case.
        len -= num2 * size2;
        map_range_in_pgtbl(pgtbl, va, pa, len, flags);
        return 0;
        /* LAB 2 TODO 4 END */
}

int unmap_range_in_pgtbl_huge(void *pgtbl, vaddr_t va, size_t len)
{
        /* LAB 2 TODO 4 BEGIN */
        // ptp_t* cur_ptp = (ptp_t*) pgtbl;
        // u64 num = 0;
        // if (len % PAGE_SIZE == 0) num = len / PAGE_SIZE;
        // else num = len / PAGE_SIZE + 1;
        // while (num > 0) {
        //         ptp_t* cur_ptp = (ptp_t*) pgtbl;
        //         pte_t* pte;
        //         int res0 = get_next_ptp(cur_ptp, 0, va, &cur_ptp, &pte, false);
        //         if (res0 == -ENOMAPPING) {
        //                 num -= PTP_ENTRIES * PTP_ENTRIES * PTP_ENTRIES;
        //                 va += PTP_ENTRIES * PTP_ENTRIES * PTP_ENTRIES * PAGE_SIZE;
        //                 continue;
        //         }
        //         // handle 1GB huge page case.
        //         int res1 = get_next_ptp(cur_ptp, 1, va, &cur_ptp, &pte, false);
        //         if (res1 == -ENOMAPPING) {
        //                 num -= PTP_ENTRIES * PTP_ENTRIES;
        //                 va += PTP_ENTRIES * PTP_ENTRIES * PAGE_SIZE;
        //                 continue;
        //         }
        //         if (res1 == BLOCK_PTP) {
        //                 num -= PTP_ENTRIES * PTP_ENTRIES;
        //                 va += PTP_ENTRIES * PTP_ENTRIES * PAGE_SIZE;
        //                 // invalid block.
        //                 // pte->l2_block.is_valid = 0;
        //                 // cur_ptp->ent[GET_L1_INDEX(va)].pte = 0;
        //                 pte->pte = 0;
        //                 continue;
        //         }
        //         int res2 = get_next_ptp(cur_ptp, 2, va, &cur_ptp, &pte, false);
        //         if (res2 == -ENOMAPPING) {
        //                 num -= PTP_ENTRIES;
        //                 va += PTP_ENTRIES * PAGE_SIZE;
        //                 continue;
        //         }
        //         if (res2 == BLOCK_PTP) {
        //                 num -= PTP_ENTRIES;
        //                 va += PTP_ENTRIES * PAGE_SIZE;
        //                 // pte->l2_block.is_valid = 0;
        //                 // cur_ptp->ent[GET_L2_INDEX(va)].pte = 0;
        //                 pte->pte = 0;
        //                 continue;
        //         }
        //         cur_ptp->ent[GET_L3_INDEX(va)].pte = 0;
        //         va += PAGE_SIZE;
        //         num--;
        //         if (num == 0) break;
        // }
        // return 0;
        u64 num = 0;
        if (len % PAGE_SIZE == 0) num = len / PAGE_SIZE;
        else num = len / PAGE_SIZE + 1;
        u64 i = 0;
        for (i = 0; i < num; ++i) {
                ptp_t* cur_ptp = (ptp_t*) pgtbl;
                pte_t* pte;
                int res0 = get_next_ptp(cur_ptp, 0, va, &cur_ptp, &pte, false);
                u64 num1 = PTP_ENTRIES * PTP_ENTRIES * PTP_ENTRIES;
                if (res0 == -ENOMAPPING) {
                        i += num1;
                        i--;
                        va += num1 * PAGE_SIZE;
                        continue;
                }
                int res1 = get_next_ptp(cur_ptp, 1, va, &cur_ptp, &pte, false);
                u64 num2 = PTP_ENTRIES * PTP_ENTRIES;
                if (res1 == -ENOMAPPING) {
                        i += num2;
                        i--;
                        va += num2 * PAGE_SIZE;
                        continue;
                }
                if (res1 == BLOCK_PTP) {
                        i += num2;
                        i--;
                        va += num2 * PAGE_SIZE;
                        pte->pte = 0;
                        continue;
                }
                int res2 = get_next_ptp(cur_ptp, 2, va, &cur_ptp, &pte, false);
                u64 num3 = PTP_ENTRIES;
                if (res2 == -ENOMAPPING) {
                        i += num3;
                        i--;
                        va += num3 * PAGE_SIZE;
                        continue;
                }
                if (res2 == BLOCK_PTP) {
                        i += num3;
                        i--;
                        va += num3 * PAGE_SIZE;
                        pte->pte = 0;
                        continue;
                }
                cur_ptp->ent[GET_L3_INDEX(va)].pte = 0;
                va += PAGE_SIZE;
        }
        return 0;
        /* LAB 2 TODO 4 END */
}

void reconfig_page_table() {
        vmr_prop_t flags_0 = VMR_READ | VMR_WRITE | VMR_EXEC;
        vmr_prop_t flags_1 = VMR_READ | VMR_WRITE | VMR_DEVICE | VMR_EXEC;
        size_t len_0 = 0x3f000000;
        size_t len_1 = 0x80000000 - 0x3f000000;
        // unmap_range_in_pgtbl((void*) 0x00000000, 0x00000000, 0x80000000);
        int order = 0;
        void* pgtbl0 = get_pages(order);
        // reconfig normal memory part.
        int ret_0 = map_range_in_pgtbl(pgtbl0, KERNEL_OFFSET, 0, len_0, flags_0);
        void* pgtbl1 = get_pages(order);
        // reconfig device memory part.
        int ret_1 = map_range_in_pgtbl(pgtbl1, KERNEL_OFFSET + len_0, len_0, len_1, flags_1);
}

#ifdef CHCORE_KERNEL_TEST
#include <mm/buddy.h>
#include <lab.h>
void lab2_test_page_table(void)
{
        vmr_prop_t flags = VMR_READ | VMR_WRITE;
        {
                bool ok = true;
                void *pgtbl = get_pages(0);
                memset(pgtbl, 0, PAGE_SIZE);
                paddr_t pa;
                pte_t *pte;
                int ret;

                ret = map_range_in_pgtbl(
                        pgtbl, 0x1001000, 0x1000, PAGE_SIZE, flags);
                lab_assert(ret == 0);

                ret = query_in_pgtbl(pgtbl, 0x1001000, &pa, &pte);
                lab_assert(ret == 0 && pa == 0x1000);
                lab_assert(pte && pte->l3_page.is_valid && pte->l3_page.is_page
                           && pte->l3_page.SH == INNER_SHAREABLE);
                ret = query_in_pgtbl(pgtbl, 0x1001050, &pa, &pte);
                lab_assert(ret == 0 && pa == 0x1050);

                ret = unmap_range_in_pgtbl(pgtbl, 0x1001000, PAGE_SIZE);
                lab_assert(ret == 0);
                ret = query_in_pgtbl(pgtbl, 0x1001000, &pa, &pte);
                lab_assert(ret == -ENOMAPPING);

                free_page_table(pgtbl);
                lab_check(ok, "Map & unmap one page");
        }
        {
                bool ok = true;
                void *pgtbl = get_pages(0);
                memset(pgtbl, 0, PAGE_SIZE);
                paddr_t pa;
                pte_t *pte;
                int ret;
                size_t nr_pages = 10;
                size_t len = PAGE_SIZE * nr_pages;

                ret = map_range_in_pgtbl(pgtbl, 0x1001000, 0x1000, len, flags);
                lab_assert(ret == 0);
                ret = map_range_in_pgtbl(
                        pgtbl, 0x1001000 + len, 0x1000 + len, len, flags);
                lab_assert(ret == 0);

                for (int i = 0; i < nr_pages * 2; i++) {
                        ret = query_in_pgtbl(
                                pgtbl, 0x1001050 + i * PAGE_SIZE, &pa, &pte);
                        lab_assert(ret == 0 && pa == 0x1050 + i * PAGE_SIZE);
                        lab_assert(pte && pte->l3_page.is_valid
                                   && pte->l3_page.is_page);
                }

                ret = unmap_range_in_pgtbl(pgtbl, 0x1001000, len);
                lab_assert(ret == 0);
                ret = unmap_range_in_pgtbl(pgtbl, 0x1001000 + len, len);
                lab_assert(ret == 0);

                for (int i = 0; i < nr_pages * 2; i++) {
                        ret = query_in_pgtbl(
                                pgtbl, 0x1001050 + i * PAGE_SIZE, &pa, &pte);
                        lab_assert(ret == -ENOMAPPING);
                }

                free_page_table(pgtbl);
                lab_check(ok, "Map & unmap multiple pages");
        }
        {
                bool ok = true;
                void *pgtbl = get_pages(0);
                memset(pgtbl, 0, PAGE_SIZE);
                paddr_t pa;
                pte_t *pte;
                int ret;
                /* 1GB + 4MB + 40KB */
                size_t len = (1 << 30) + (4 << 20) + 10 * PAGE_SIZE;

                ret = map_range_in_pgtbl(
                        pgtbl, 0x100000000, 0x100000000, len, flags);
                lab_assert(ret == 0);
                ret = map_range_in_pgtbl(pgtbl,
                                         0x100000000 + len,
                                         0x100000000 + len,
                                         len,
                                         flags);
                lab_assert(ret == 0);

                for (vaddr_t va = 0x100000000; va < 0x100000000 + len * 2;
                     va += 5 * PAGE_SIZE + 0x100) {
                        ret = query_in_pgtbl(pgtbl, va, &pa, &pte);
                        lab_assert(ret == 0 && pa == va);
                }

                ret = unmap_range_in_pgtbl(pgtbl, 0x100000000, len);
                lab_assert(ret == 0);
                ret = unmap_range_in_pgtbl(pgtbl, 0x100000000 + len, len);
                lab_assert(ret == 0);

                for (vaddr_t va = 0x100000000; va < 0x100000000 + len;
                     va += 5 * PAGE_SIZE + 0x100) {
                        ret = query_in_pgtbl(pgtbl, va, &pa, &pte);
                        lab_assert(ret == -ENOMAPPING);
                }

                free_page_table(pgtbl);
                lab_check(ok, "Map & unmap huge range");
        }
        {
                bool ok = true;
                void *pgtbl = get_pages(0);
                memset(pgtbl, 0, PAGE_SIZE);
                paddr_t pa;
                pte_t *pte;
                int ret;
                /* 1GB + 4MB + 40KB */
                size_t len = (1 << 30) + (4 << 20) + 10 * PAGE_SIZE;
                size_t free_mem, used_mem;

                free_mem = get_free_mem_size_from_buddy(&global_mem[0]);
                ret = map_range_in_pgtbl_huge(
                        pgtbl, 0x100000000, 0x100000000, len, flags);
                lab_assert(ret == 0);
                used_mem =
                        free_mem - get_free_mem_size_from_buddy(&global_mem[0]);
                lab_assert(used_mem < PAGE_SIZE * 8);

                for (vaddr_t va = 0x100000000; va < 0x100000000 + len;
                     va += 5 * PAGE_SIZE + 0x100) {
                        ret = query_in_pgtbl(pgtbl, va, &pa, &pte);
                        lab_assert(ret == 0 && pa == va);
                }

                ret = unmap_range_in_pgtbl_huge(pgtbl, 0x100000000, len);
                lab_assert(ret == 0);

                for (vaddr_t va = 0x100000000; va < 0x100000000 + len;
                     va += 5 * PAGE_SIZE + 0x100) {
                        ret = query_in_pgtbl(pgtbl, va, &pa, &pte);
                        lab_assert(ret == -ENOMAPPING);
                }

                free_page_table(pgtbl);
                lab_check(ok, "Map & unmap with huge page support");
        }
        printk("[TEST] Page table tests finished\n");
}
#endif /* CHCORE_KERNEL_TEST */