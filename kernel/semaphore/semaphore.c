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

#include <semaphore/semaphore.h>
#include <sched/sched.h>
#include <object/thread.h>
#include <sched/context.h>
#include <irq/irq.h>

void init_sem(struct semaphore *sem)
{
        sem->sem_count = 0;
        sem->waiting_threads_count = 0;
        init_list_head(&sem->waiting_threads);
}

/*
 * Lab4
 * Return 0 if wait successfully, -EAGAIN otherwise
 * Hint: use sched() and eret_to_thread if current thread should be blocked.
 * You should also update the state of current thread to TS_WAITING
 * and set the return value of the current thread.
 * Besides, you should `obj_put` the sem before eret to the new thread.
 */
s32 wait_sem(struct semaphore *sem, bool is_block)
{
        s32 ret = 0;
        /* LAB 4 TODO BEGIN */
        // if (!is_block) {
        //         if (sem->sem_count == 0) return -EAGAIN;
        // }
        // if (is_block) {
        //         if (sem->sem_count == 0) {
        //                 // block the current thread.
        //                 sem->waiting_threads_count++;
        //                 // add current thread to sem-waiting queue.
        //                 list_append(&(current_thread->sem_queue_node), &(sem->waiting_threads));
        //                 // make the current thread waiting and begin to sched a new thread to execute.
        //                 current_thread->thread_ctx->state = TS_WAITING;
        //                 current_thread->thread_ctx->sc->budget = 0;
        //                 obj_put(sem);
        //                 sched();
        //                 eret_to_thread(switch_context());
        //         }
        // }
        // // as the use of big-kernel-lock, dec directly.
        // sem->sem_count--;
        if (sem->sem_count == 0) {
                if (is_block) {
                        sem->waiting_threads_count++;
                        list_append(&(current_thread->sem_queue_node), &(sem->waiting_threads));
                        current_thread->thread_ctx->state = TS_WAITING;
                        // current_thread->thread_ctx->sc->budget = 0;
                        // arch_set_thread_return(current_thread, 0);
                        obj_put(sem);
                        sched();
                        eret_to_thread(switch_context());
                } else {
                        return -EAGAIN;
                }
        } else {
                sem->sem_count--;
        }
        /* LAB 4 TODO END */
        return ret;
}

/*
 * Lab4
 * Return 0 if signal successfully
 * Hint: use `list_entry` to find the wakeup target in the waiting_threads list
 * Remember to delete the thread from the list using `list_del`.
 * Enqueue it to the ready queue rather than directly switch to it.
 */
s32 signal_sem(struct semaphore *sem)
{
        /* LAB 4 TODO BEGIN */
        // // no thread need to wait, that means no one is waiting for th new slot,
        // // so just add the slot number directly.
        // if (list_empty(&(sem->waiting_threads))) {
        //         sem->sem_count++;
        // }
        // else {
        //         // wakeup one thread in the waiting queue.
        //         struct thread* next = list_entry(sem->waiting_threads.next, struct thread, sem_queue_node);
        //         sem->waiting_threads_count--;
        //         next->thread_ctx->state = TS_INTER;
        //         list_del(&(next->sem_queue_node));
        //         // add the newly-chosen thread into ready queue.
        //         sched_enqueue(next);
        // }
        if (sem->waiting_threads_count == 0) {
                // kinfo("[DEBUG] get into signal_sem sem->waiting 0 case!\n");
                sem->sem_count++;
                return 0;
        } else {
                // kinfo("[DEBUG] get into signal_sem sem->waiting not 0 case!\n");
                sem->waiting_threads_count--;
                struct thread* to_sched = list_entry(sem->waiting_threads.next, struct thread, sem_queue_node);
                list_del(&to_sched->sem_queue_node);
                // to_sched->thread_ctx->state = TS_INTER;
                sched_enqueue(to_sched);
        }
        /* LAB 4 TODO END */
        return 0;
}

s32 sys_create_sem(void)
{
        struct semaphore *sem = NULL;
        int sem_cap = 0;
        int ret = 0;

        sem = obj_alloc(TYPE_SEMAPHORE, sizeof(*sem));
        if (!sem) {
                ret = -ENOMEM;
                goto out_fail;
        }
        init_sem(sem);

        sem_cap = cap_alloc(current_cap_group, sem, 0);
        if (sem_cap < 0) {
                ret = sem_cap;
                goto out_free_obj;
        }
        // kinfo("sem_cap after init = %d\n", sem_cap);

        return sem_cap;
out_free_obj:
        obj_free(sem);
out_fail:
        return ret;
}

s32 sys_wait_sem(u32 sem_cap, bool is_block)
{
        struct semaphore *sem = NULL;
        int ret;

        sem = obj_get(current_thread->cap_group, sem_cap, TYPE_SEMAPHORE);
        if (!sem) {
                ret = -ECAPBILITY;
                goto out;
        }
        ret = wait_sem(sem, is_block);
        // kinfo("sem num for wait %d = %d\n", sem_cap, sem->sem_count);
        obj_put(sem);
out:
        return ret;
}

s32 sys_signal_sem(u32 sem_cap)
{
        struct semaphore *sem = NULL;
        int ret;
        sem = obj_get(current_thread->cap_group, sem_cap, TYPE_SEMAPHORE);
        if (!sem) {
                ret = -ECAPBILITY;
                goto out;
        }
        ret = signal_sem(sem);
        // kinfo("sem num for signal %d = %d\n", sem_cap, sem->sem_count);
        obj_put(sem);
out:
        return ret;
}
