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

#include <common/macro.h>
#include <common/util.h>
#include <common/list.h>
#include <common/errno.h>
#include <common/kprint.h>
#include <common/types.h>
#include <lib/printk.h>
#include <mm/vmspace.h>
#include <mm/kmalloc.h>
#include <mm/mm.h>
#include <mm/vmspace.h>
#include <arch/mmu.h>
#include <object/thread.h>
#include <object/cap_group.h>
#include <sched/context.h>

struct v_node {
        struct list_head node;
        struct vmregion* vmr;
        int idx;
};

struct lru_node {
        struct list_head node;
        int idx;
        struct v_node* vaddr;
};

void* real_mem;
// the start node of the "disk" mem pages.
// this is a bi-direct list and should form a cycle to do clock algorithm.
struct lru_node* lru_head;
// the current number of "disk" pages in the lru list.
int lru_num;

void lru_init()
{
        real_mem = kmalloc(PAGE_SIZE * LRU_SIZE);
        memset(real_mem, 0, PAGE_SIZE * LRU_SIZE);
        lru_head = (struct lru_node*)(kmalloc(sizeof(struct lru_node)));
        init_list_head(&(lru_head->node));
        lru_head->idx = -1;
        lru_num = 0;
        real_mem = kmalloc(PAGE_SIZE * LRU_SIZE);
        memset(real_mem, 0, PAGE_SIZE * LRU_SIZE);
}

// the clock algorithm to get next swap page by LRU policy.
int clock_algo()
{
        int allocated[LRU_SIZE];
        for (int i = 0; i < LRU_SIZE; ++i) allocated[i] = 0;
        struct lru_node* start = (lru_head->node).next;
        // when this loop finished, all nodes have been traversed.
        while (start != &(lru_head->node)) {
                printk("[DEBUG] idx = %d\n", start->idx);
                allocated[start->idx] = 1;
                start = (start->node).next;
        }
        for (int i = 0; i < LRU_SIZE; ++i) {
                if (allocated[i] == 0) {
                        return i;
                }
        }
        printk("[DEBUG] get here to return -1 in func clock_algo!\n");
        return -1;
}

// swap out a page.
void swap_out_page(struct vmspace* vmspace, struct lru_node* evicted)
{
        // remove the chosen evicted node from list.
        printk("[DEBUG] the idx to evict = %d\n", evicted->idx);
        list_del(&(evicted->node));
        struct v_node* start = evicted->vaddr;
        unmap_range_in_pgtbl(vmspace->pgtbl, start->vmr->start + start->idx * PAGE_SIZE, PAGE_SIZE);
        start = (struct v_node*) ((start->node).next);
        // unmap existed map for the page will be evicted.
        while (start != evicted->vaddr) {
                unmap_range_in_pgtbl(vmspace->pgtbl, start->vmr->start + start->idx * PAGE_SIZE, PAGE_SIZE);
                start = (struct v_node*) ((start->node).next);
        }
        // get a new physical page to act "write back" action.
        vaddr_t new_va = (vaddr_t)(get_pages(0));
        paddr_t pa = virt_to_phys(new_va);
        printk("[DEBUG] new pa after evict a page = 0x%lx\n", pa);
        memcpy(phys_to_virt(pa), (real_mem) + PAGE_SIZE * evicted->idx, PAGE_SIZE);
        struct v_node* start1 = evicted->vaddr;
        radix_del(start1->vmr->pmo->radix, start1->idx);
        commit_page_to_pmo(start1->vmr->pmo, start1->idx, pa);
        start1 = (struct v_node*) ((start1->node).next);
        while(start1 != evicted->vaddr) {
                radix_del(start1->vmr->pmo->radix, start1->idx);
                commit_page_to_pmo(start1->vmr->pmo, start1->idx, pa);
                start1 = (struct v_node*) ((start1->node).next);
        }
        kfree(evicted);
        // /*-- DEBUG part --*/
        // struct lru_node* test = (lru_head->node).next;
        // printk("[DEBUG] get here!\n");
        // while(test != &(lru_head->node)) {
        //         printk("[DEBUG] the idx of node after evict = %d\n", test->idx);
        //         test = test->node.next;
        // }
        lru_num--;
}

// used to init a vaddr var maintained in lru_node
void init_lru_vaddr(struct lru_node* new_alloc, struct vmregion* vmr, int index)
{
        new_alloc->vaddr = (struct v_node*)(kmalloc(sizeof(struct v_node)));
        init_list_head(&(new_alloc->vaddr->node));
        new_alloc->vaddr->idx = index;
        new_alloc->vaddr->vmr = vmr;
}

int handle_trans_fault(struct vmspace *vmspace, vaddr_t fault_addr)
{
        struct vmregion *vmr;
        struct pmobject *pmo;
        paddr_t pa;
        u64 offset;
        u64 index;
        int ret = 0;
        // find corresponding vmregion for the fault vaddr.
        printk("[DEBUG] fault_addr = 0x%lx\n", fault_addr);
        vmr = find_vmr_for_va(vmspace, fault_addr);
        if (vmr == NULL) {
                printk("handle_trans_fault: no vmr found for va 0x%lx!\n",
                       fault_addr);
                kinfo("process: %p\n", current_cap_group);
                print_thread(current_thread);
                kinfo("faulting IP: 0x%lx, SP: 0x%lx\n",
                      arch_get_thread_next_ip(current_thread),
                      arch_get_thread_stack(current_thread));

                kprint_vmr(vmspace);
                kwarn("TODO: kill such faulting process.\n");
                return -ENOMAPPING;
        }

        // init lru part.
        if (lru_head == NULL) {
                lru_init();
        }

        pmo = vmr->pmo;
        switch (pmo->type) {
        case PMO_ANONYM:
        case PMO_SHM: {
                vmr_prop_t perm;

                perm = vmr->perm;

                /* Get the offset in the pmo for faulting addr */
                offset = ROUND_DOWN(fault_addr, PAGE_SIZE) - vmr->start;
                BUG_ON(offset >= pmo->size);

                /* Get the index in the pmo radix for faulting addr */
                index = offset / PAGE_SIZE;

                fault_addr = ROUND_DOWN(fault_addr, PAGE_SIZE);
                /* LAB 3 TODO BEGIN */
                // get the corresponding physical page.
                pa = get_page_from_pmo(pmo, index);
                /* LAB 3 TODO END */
                if (pa == 0) {
                        if (USE_LRU == 1) {
                                // the case using LRU policy.
                                if (lru_num >= LRU_SIZE) {
                                        // every time try to evict the node at the HEAD of the lru_list.
                                        swap_out_page(vmspace, lru_head->node.next);
                                }
                                struct lru_node* new_alloc = (struct lru_node*)(kmalloc(sizeof(struct lru_node)));
                                int new_idx = clock_algo();
                                new_alloc->idx = new_idx;
                                printk("[DEBUG] new idx in case pa = 0 is %d\n", new_idx);
                                // insert the newly allocated node into the TAIL of lru_list.
                                list_append(&(new_alloc->node), &(lru_head->node));
                                // init vaddr.
                                new_alloc->vaddr = (struct v_node*)(kmalloc(sizeof(struct v_node)));
                                init_list_head(&(new_alloc->vaddr->node));
                                new_alloc->vaddr->idx = index;
                                new_alloc->vaddr->vmr = vmr;
                                pa = virt_to_phys(real_mem) + new_alloc->idx * PAGE_SIZE;
                                printk("new pa in case pa = 0 is = 0x%lx\n", pa);
                                memset((void*) phys_to_virt(pa), 0, PAGE_SIZE);
                                commit_page_to_pmo(pmo, index, pa);
                                map_range_in_pgtbl(vmspace->pgtbl, fault_addr, pa, PAGE_SIZE, perm);
                                lru_num++;
                                printk("finish lru successfully for pa = 0 case!\n");
                        }
                        /* Not committed before. Then, allocate the physical
                         * page. */
                        /* LAB 3 TODO BEGIN */
                        else {
                                // if there is no LRU, pages are infinite to use.
                                void* new_page = get_pages(0);
                                pa = virt_to_phys(new_page);
                                // create new mapping relation.
                                commit_page_to_pmo(pmo, index, pa);
                                map_range_in_pgtbl(vmspace->pgtbl, fault_addr, pa, PAGE_SIZE, perm);
                        }
                        /* LAB 3 TODO END */
#ifdef CHCORE_LAB3_TEST
                        printk("Test: Test: Successfully map for pa 0\n");
#endif
                } else {
                        /*
                         * pa != 0: the faulting address has be committed a
                         * physical page.
                         *
                         * For concurrent page faults:
                         *
                         * When type is PMO_ANONYM, the later faulting threads
                         * of the process do not need to modify the page
                         * table because a previous faulting thread will do
                         * that. (This is always true for the same process)
                         * However, if one process map an anonymous pmo for
                         * another process (e.g., main stack pmo), the faulting
                         * thread (e.g, in the new process) needs to update its
                         * page table.
                         * So, for simplicity, we just update the page table.
                         * Note that adding the same mapping is harmless.
                         *
                         * When type is PMO_SHM, the later faulting threads
                         * needs to add the mapping in the page table.
                         * Repeated mapping operations are harmless.
                         */
                        /* LAB 3 TODO BEGIN */
                        // as the fault addr-page has commited a page, just remap for this page directly.
                        if (USE_LRU == 1) {
                                // the case using LRU policy.
                                // TODO: for now this part is not finished....
                                pa = ROUND_DOWN(pa, PAGE_SIZE);
                                printk("pa = 0x%lx\n", pa);
                                printk("real_mem = 0x%lx\n", (virt_to_phys(real_mem)));
                                bool in_mem = ((pa >=  virt_to_phys(real_mem)) && (pa < virt_to_phys(real_mem) + PAGE_SIZE * LRU_SIZE));
                                if (!in_mem) {
                                        printk("[DEBUG] get into not in mem case!\n");
                                        // the corresponding page had been evicted out.
                                        // printk("[DEBUG] lru num 1 = %d\n", lru_num);
                                        if (lru_num >= LRU_SIZE) {
                                                swap_out_page(vmspace, (struct lru_node*) (lru_head->node.next));
                                        }
                                        // printk("[DEBUG] lru num 2 = %d\n", lru_num);
                                        struct lru_node* new_alloc = (struct lru_node*)(kmalloc(sizeof(struct lru_node)));
                                        int new_idx = clock_algo();
                                        printk("[DEBUG] new_idx for pa not 0 = %d\n", new_idx);
                                        new_alloc->idx = new_idx;
                                        list_append(&(new_alloc->node), &(lru_head->node));
                                        new_alloc->vaddr = (struct v_node*)(kmalloc(sizeof(struct v_node)));
                                        init_list_head(&(new_alloc->vaddr->node));
                                        new_alloc->vaddr->idx = index;
                                        new_alloc->vaddr->vmr = vmr;
                                        paddr_t cur_pa = virt_to_phys(real_mem) + PAGE_SIZE * new_alloc->idx;
                                        memcpy((void*) phys_to_virt(cur_pa), (void*) phys_to_virt(pa), PAGE_SIZE);
                                        radix_del(pmo->radix, index);
                                        commit_page_to_pmo(pmo, index, cur_pa);
                                        map_range_in_pgtbl(vmspace->pgtbl, fault_addr, cur_pa, PAGE_SIZE, perm);
                                        lru_num++;
                                        printk("successfully map again for pa not 0 when the page is evicted\n");
                                        // map_range_in_pgtbl(vmspace->pgtbl, fault_addr, pa, PAGE_SIZE, perm);
                                }
                                else {
                                        printk("[DEBUG] get into in-mem case !\n");
                                        // the case that this page is not evicted out from the "disk" memory.
                                        int disk_idx = (pa -  virt_to_phys(real_mem)) / PAGE_SIZE;
                                        struct lru_node* start = lru_head->node.next;
                                        // find the node according to its idx.
                                        while (start != &(lru_head->node)) {
                                                if (start->idx == disk_idx) break;
                                                start = start->node.next;
                                        }
                                        // add a new vm mapping relation related to this "disk" page.
                                        struct v_node* new_va = (struct v_node*)(kmalloc(sizeof(struct v_node)));
                                        new_va->idx = index;
                                        new_va->vmr = vmr;
                                        list_append(&(new_va->node), &(start->vaddr->node));
                                        map_range_in_pgtbl(vmspace->pgtbl, fault_addr, pa, PAGE_SIZE, perm);
                                        // add the most-recently visited node at the end of the list.
                                        list_del(&(start->node));
                                        list_append(&(start->node), &(lru_head->node));
                                        printk("successfully map again for pa not 0 when the page is not evicted\n");
                                }
                        }
                        else map_range_in_pgtbl(vmspace->pgtbl, fault_addr, pa, PAGE_SIZE, perm);
                        /* LAB 3 TODO END */
#ifdef CHCORE_LAB3_TEST
                        printk("Test: Test: Successfully map for pa not 0\n");
#endif
                }

                /* Cortex A53 has VIPT I-cache which is inconsistent with
                 * dcache. */
#ifdef CHCORE_ARCH_AARCH64
                if (vmr->perm & VMR_EXEC) {
                        extern void arch_flush_cache(u64, s64, int);
                        /*
                         * Currently, we assume the fauling thread is running on
                         * the CPU. Thus, we flush cache by using user VA.
                         */
                        BUG_ON(current_thread->vmspace != vmspace);
                        /* 4 means flush idcache. */
                        arch_flush_cache(fault_addr, PAGE_SIZE, 4);
                }
#endif

                break;
        }
        case PMO_FORBID: {
                kinfo("Forbidden memory access (pmo->type is PMO_FORBID).\n");
                BUG_ON(1);
                break;
        }
        default: {
                kinfo("handle_trans_fault: faulting vmr->pmo->type"
                      "(pmo type %d at 0x%lx)\n",
                      vmr->pmo->type,
                      fault_addr);
                kinfo("Currently, this pmo type should not trigger pgfaults\n");
                kprint_vmr(vmspace);
                BUG_ON(1);
                return -ENOMAPPING;
        }
        }

        return ret;
}