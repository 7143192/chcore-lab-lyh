# `OS-lab4`实验报告

- 思考题 1：阅读汇编代码`kernel/arch/aarch64/boot/raspi3/init/start.S`。说明`ChCore`是如何选定主CPU，并阻塞其他其他CPU的执行的。

- **回答1**：

  - 如何选定主`CPU`:

    `mpidr_el1`寄存器中会存储当前`cpuid`,只有首先通过`mrs`指令将这个数值存入`x8`寄存器中，之后通过`cbz x8, primary`指令，只有在`x8 == 0`的时候才会跳转到`primary`处继续执行，而这个`primary`部分就是进行主`CPU`的初始化的。

  - 如何阻塞其他`CPU`的执行：

    通过维护`secondary_boot_flag`来进行阻塞。最开始这个变量数值为0，会使得其他`CPU`在一直循环等待被唤醒，之后当主`CPU`初始化结束之后，会调用`enable_smp_cores`来初始化其他的`CPU`,此时会将`secondary_boot_flag`设置为1，使得一个非主`CPU`可以完成初始化。

  

- 思考题 2：阅读汇编代码`kernel/arch/aarch64/boot/raspi3/init/start.S, init_c.c`以及`kernel/arch/aarch64/main.c`，解释用于阻塞其他CPU核心的`secondary_boot_flag`是物理地址还是虚拟地址？是如何传入函数`enable_smp_cores`中，又该如何赋值的（考虑虚拟地址/物理地址）？

- **回答2**：

  - 是**虚拟地址**。

  - `secondary_boot_flag`并不是直接传入`enable_smp_cores`函数的，而是先向`enable_smp_cores`中传入一个**物理地址**`boot_flag`,之后在函数`enable_smp_cores`内部通过`phys_to_virt()`函数转化成虚拟地址的。

    

- 练习题 3：完善主CPU激活各个其他CPU的函数：`enable_smp_cores`和`kernel/arch/aarch64/main.c`中的`secondary_start`。

- **回答3**：

  - `enable_smp_cores`:先将`secondary_boot_flag`设置为1允许当前`CPU`开始初始化，之后通过一个忙等的循环来等待当前`CPU`真正启动成功之后才允许下一个`CPU`开始初始化。

    ```c
    ......
    for (i = 0; i < PLAT_CPU_NUM; i++) {
            		......
                    /* LAB 4 TODO BEGIN */
                    secondary_boot_flag[i] = 1;
                    /* LAB 4 TODO END */
    				......
                    /* LAB 4 TODO BEGIN */
                    while (cpu_status[i] == cpu_hang) {
                            continue;
                    }
                    /* LAB 4 TODO END */
                    ......
            }
    ......
    ```

  - `secondary_start`:

    ```c
    void secondary_start(void)
    {
    		......
            /* LAB 4 TODO BEGIN: Set the cpu_status */
            cpu_status[cpuid] = cpu_run;
            /* LAB 4 TODO END */
    		......
            /* LAB 4 TODO BEGIN */
            timer_init();
            /* LAB 4 TODO END */
    		......
    }
    ```

  

- 练习题 4：请熟悉排号锁的基本算法，并在`kernel/arch/aarch64/sync/ticket.c`中完成`unlock`和`is_locked`的代码；在`kernel/arch/aarch64/sync/ticket.c`中实现`kernel_lock_init`、`lock_kernel`和`unlock_kernel`；在适当的位置调用`lock_kernel`；判断什么时候需要放锁，添加`unlock_kernel`。

- **回答4**：

  - `ticket.c`中相关实现：由于同一时刻只会有一个线程拿锁，所以`unlock`的时候不再需要保护直接修改数值，`is_locked`只需要判断`lock->owner`与`lock->next`是否相等即可，因为如果已经有人拿锁的话，`owner`一定会小于`next`。`kernel_lock`部分，只需要直接复用上述相关接口即可。

    ```c
    ......
    void unlock(struct lock *l)
    {
            ......
            /* LAB 4 TODO BEGIN */
            // only one thread can hold the lock at the same time.
            lock->owner++;
            /* LAB 4 TODO END */
    }
    
    int is_locked(struct lock *l)
    {
            int ret = 0;
            struct lock_impl *lock = (struct lock_impl *)l;
            /* LAB 4 TODO BEGIN */
            if (lock->owner != lock->next) return 1;
            /* LAB 4 TODO END */
            return ret;
    }
    
    struct lock big_kernel_lock;
    void kernel_lock_init(void)
    {
            u32 ret = 0;
            /* LAB 4 TODO BEGIN */
            ret = lock_init(&big_kernel_lock);
            /* LAB 4 TODO END */
            BUG_ON(ret != 0);
    }
    void lock_kernel(void)
    {
            /* LAB 4 TODO BEGIN */
            lock(&big_kernel_lock);
            /* LAB 4 TODO END */
    }
    void unlock_kernel(void)
    {
            BUG_ON(!is_locked(&big_kernel_lock));
            /* LAB 4 TODO BEGIN */
            unlock(&big_kernel_lock);
            /* LAB 4 TODO END */
    }
    ```

  - 关于`lock_kernel`的使用：

    根据文档的提示以及代码框架中相关注释提示，添加`lock_kernel`函数调用即可。

  - 关于`unlock_kernel`的使用：

    `unlock_kernel`主要是在退出内核态返回用户态之前要放锁，从而保证用户态应用可以正确执行，所以需要在进行特权态切换的时候进行`unlock_kernel`,主要逻辑位于`irq_entry.S`文件中。

    ```
    sync_el0_64:
    	......
    	/*unlock here before retuen to user mode.*/
    	bl unlock_kernel
    	exception_exit
    
    el0_syscall:
    	......
    	/*unlock here.*/
    	bl unlock_kernel
    	exception_exit
    ......
    /* void eret_to_thread(u64 sp) */
    BEGIN_FUNC(__eret_to_thread)
    	......
    	/*unlock here.*/
    	bl unlock_kernel
    	exception_exit
    END_FUNC(__eret_to_thread)

  

- 思考题 5：在`el0_syscall`调用`lock_kernel`时，在栈上保存了寄存器的值。这是为了避免调用`lock_kernel`时修改这些寄存器。在`unlock_kernel`时，是否需要将寄存器的值保存到栈中，试分析其原因。

- **回答5**：

  **不需要**。在执行`unlock_kernel`之后即通过`exception_exit`返回用户态，所以就不必在此保存在内核态下的寄存器中的数据了。

  

- 思考题 6：为何`idle_threads`不会加入到等待队列中？请分析其原因？

- **回答6**：

  `idle_threads`主要是为了防止内核在就绪队列为空的时候进行忙等而导致内核阻塞的情况发生。但是如果将`idle_thread`加入就绪队列中，由于使用`RR`调度策略，所以内核可能每过一段时间就会主动调度`idle_thread`进行无意义工作(即使还有其他线程未被调度)，从而可能使得内核性能下降。

  

- 练习题 7：完善`kernel/sched/policy_rr.c`中的调度功能.

- **回答7**：(**注：由于是完成全部之后才完成文档部分，所以代码中可能会包含后面的任务才要求完成的部分**)

  - `rr_shed_enqueue`：先检查参数线程的状态，如果已经是`idle`或者已经入队(`ready`),则不需要再次入队，否则将其添加到就绪队列尾部。(如果未设置过亲和度，则将亲和度设置为当前的`cpuid`,否则使用设置的亲和度作为等待就绪队列的索引。)
  - `rr_sched_dequeue`：先检查参数线程的状态，如果状态合理，则将其从对应的等待队列中出队即可。
  - `rr_choose_thread`：在当前`cpuid`对应的队列中选取一个用于调度，如果非空，选择队列头部线程，如果已经为空，返回对应的`idle_thread`。
  - `rr_sched`：先检查调用`sched`函数的当前线程的状态，之后选择一个线程，并跳转到这个线程。

  ```c
  ......
  int rr_sched_enqueue(struct thread *thread)
  {
          /* LAB 4 TODO BEGIN */
          if (thread == NULL || thread->thread_ctx == NULL || thread->thread_ctx->state == TS_READY) return -1;
          // if (thread->thread_ctx->state == TS_WAITING) return -1;
          if (thread->thread_ctx->type == TYPE_IDLE) {
                  return 0;
          }
          u32 cur_id = smp_get_cpu_id();
          if (cur_id < 0) return -1;
          if (cur_id >= PLAT_CPU_NUM) return -1;
          if (thread->thread_ctx->affinity == NO_AFF) {
                  thread->thread_ctx->cpuid = cur_id;
          }
          else {
                  cur_id = thread->thread_ctx->affinity;
                  if (cur_id >= PLAT_CPU_NUM) return -1;
                  if (cur_id < 0) return -1;
                  thread->thread_ctx->cpuid = cur_id;
          }
          thread->thread_ctx->state = TS_READY;
          thread->thread_ctx->thread_exit_state = TE_RUNNING;
          thread->thread_ctx->sc->budget = DEFAULT_BUDGET;
          rr_ready_queue_meta[cur_id].queue_len++;
          list_append(&(thread->ready_queue_node), &(rr_ready_queue_meta[cur_id]));
          /* LAB 4 TODO END */
          return 0;
  }
  
  int rr_sched_dequeue(struct thread *thread)
  {
          /* LAB 4 TODO BEGIN */
          if (thread == NULL || thread->thread_ctx == NULL || thread->thread_ctx->state != TS_READY) return -1;
          if (thread->thread_ctx->type == TYPE_IDLE) return -1;
          u32 cur_id = thread->thread_ctx->cpuid;
          thread->thread_ctx->state = TS_INTER;
          list_del(&(thread->ready_queue_node));
          rr_ready_queue_meta[cur_id].queue_len--;
          /* LAB 4 TODO END */
          return 0;
  }
  
  struct thread *rr_sched_choose_thread(void)
  {
          struct thread *thread = NULL;
          /* LAB 4 TODO BEGIN */
          int id = smp_get_cpu_id();
          if (list_empty(&(rr_ready_queue_meta[id].queue_head))) {
                  thread = &(idle_threads[id]);
                  return thread;
          }
          thread = list_entry((rr_ready_queue_meta[id].queue_head.next), struct thread, ready_queue_node);
          int res = rr_sched_dequeue(thread);
          if (res != 0) {
                  thread = &(idle_threads[id]);
                  return thread;
          }
          /* LAB 4 TODO END */
          return thread;
  }
  ......
  int rr_sched(void)
  {
          /* LAB 4 TODO BEGIN */
          struct thread *cur = current_thread;
          if (cur && cur->thread_ctx && cur->thread_ctx->type != TYPE_IDLE) {
                  if (current_thread->thread_ctx->sc->budget > 0 && current_thread->thread_ctx->state != TS_WAITING 
                          && cur->thread_ctx->thread_exit_state != TE_EXITING) {
                          return 0;
                  }
  
                  if (cur->thread_ctx->thread_exit_state == TE_EXITING) {
                          cur->thread_ctx->state = TS_EXIT;
                          cur->thread_ctx->thread_exit_state = TE_EXITED;
                  }
                  else if (cur->thread_ctx->state != TS_WAITING) {
                          rr_sched_enqueue(cur);
                  }
          }
          cur = rr_sched_choose_thread();
          rr_sched_refill_budget(cur, DEFAULT_BUDGET);
          switch_to_thread(cur);
          /* LAB 4 TODO END */
          return 0;
  }
  ......
  ```

  

- 思考题 8：如果异常是从内核态捕获的，CPU核心不会在`kernel/arch/aarch64/irq/irq_entry.c`的`handle_irq`中获得大内核锁。但是，有一种特殊情况，即如果空闲线程（以内核态运行）中捕获了错误，则CPU核心还应该获取大内核锁。否则，内核可能会被永远阻塞。请思考一下原因。

- **回答8**：

  `idle_thread`主要作用是为了防止内核在就绪队列为空的时候进行忙等而导致内核阻塞的情况发生。因此要保证`idle_thread`始终能正确运行。但是如果在`idle_thread`捕获异常之后不会获取大内核锁，那么就使得`CPU`核心能够正常的进行调度，但是这个时候，由于`idle_thread`已经发生异常但是并没有获取大内核锁去内核态进行解决，所以`idle_thread`会挂起而无法继续执行，所以就会使得`idle_thread`失效并时的内核永远阻塞，因为当前的`CPU`核心不会释放内核锁。所以综上可知，在`idle_thread`捕获异常之后，需要获取大内核锁，进入内核态解决异常之后在恢复正常执行。



- 练习题 9：在`kernel/sched/sched.c`中实现系统调用`sys_yield()`，使用户态程序可以启动线程调度。此外，`ChCore`还添加了一个新的系统调用`sys_get_cpu_id`，其将返回当前线程运行的CPU的核心id。请在`kernel/syscall/syscall.c`文件中实现该函数。

- **回答9**：

  - `sys_yield()`:先将预算清空，之后调度一个新的线程，之后跳转到新的调度到的线程上面去。

    ```c
    void sys_yield(void)
    {
            /* LAB 4 TODO BEGIN */
            if (current_thread && current_thread->thread_ctx && current_thread->thread_ctx->sc) {
                    current_thread->thread_ctx->sc->budget = 0;
            }
            sched();
            // return to user mode.
            eret_to_thread(switch_context());
            /* LAB 4 TODO END */
            ......
    }
    ```

  - `sys_get_cpu_id()`:

    ```c
    u32 sys_get_cpu_id()
    {
            u32 cpuid = 0;
            /* LAB 4 TODO BEGIN */
            cpuid = current_thread->thread_ctx->cpuid;
            /* LAB 4 TODO END */
            return cpuid;
    }
    ```

    

- 练习题 10：定时器中断初始化的相关代码已包含在本实验的初始代码中（`timer_init`）。请在主CPU以及其他CPU的初始化流程中加入对该函数的调用。

- **回答10**：

  只需要`main.c`中的主要的初始化逻辑里面添加`timer_init()`即可。

  ```c
  void main(paddr_t boot_flag)
  {
      	......
          /* Init exception vector */
          arch_interrupt_init();
          /* LAB 4 TODO BEGIN */
          timer_init();
          /* LAB 4 TODO END */
          ......
  }
  
  void secondary_start(void)
  {
  		......
          /* LAB 4 TODO BEGIN */
          timer_init();
          /* LAB 4 TODO END */
  		......
  }
  ```

  

- 练习题 11：在`kernel/sched/sched.c`处理时钟中断的函数`sched_handle_timer_irq`中添加相应的代码，以便它可以支持预算机制。更新其他调度函数支持预算机制，不要忘记在`kernel/sched/sched.c`的`sys_yield()`中重置“预算”，确保`sys_yield`在被调用后可以立即调度当前线程。

- **回答11**：

  - `sched_handle_timer_irq`函数：在线程状态合法的情况下，直接将对应`budget`减去1即可。

    ```c
    void sched_handle_timer_irq(void)
    {
            /* LAB 4 TODO BEGIN */
            if (current_thread == NULL || current_thread->thread_ctx == NULL || current_thread->thread_ctx->sc == NULL) return ;
            if (current_thread->thread_ctx->sc->budget == 0) return ; 
            current_thread->thread_ctx->sc->budget--;
            /* LAB 4 TODO END */
    }
    ```

  - 根据`budget`机制修改调度算法：主要逻辑已经在练习题7中展示，这里补充额外的函数`rr_sched_refill_budget`:

    ```c
    static inline void rr_sched_refill_budget(struct thread *target, u32 budget)
    {
            /* LAB 4 TODO BEGIN */
            if (target == NULL || target->thread_ctx == NULL) return ;
            target->thread_ctx->sc->budget = budget;
            /* LAB 4 TODO END */
    }
    ```

    

- 练习题 12：在`kernel/object/thread.c`中实现`sys_set_affinity`和`sys_get_affinity`。完善`kernel/sched/policy_rr.c`中的调度功能，增加线程的亲和性支持（如入队时检查亲和度等，请自行考虑）。

- **回答12**：

  - `sys_set/get_affinity`:

    ```c
    int sys_set_affinity(u64 thread_cap, s32 aff)
    {
            ......
            /* LAB 4 TODO BEGIN */
            thread->thread_ctx->affinity = aff;
        	/* LAB 4 TODO END */
            ......
    }
    
    s32 sys_get_affinity(u64 thread_cap)
    {
            ......
            /* LAB 4 TODO BEGIN */
            aff = thread->thread_ctx->affinity;
            /* LAB 4 TODO END */
    		......
    }
    ```

  - 增加调度算法对于线程亲和性的支持：

    详见前面的练习题7与练习题12。

    

- 练习题 13：在`userland/servers/procm/launch.c`中填写`launch_process`函数中缺少的代码

- **回答13**：

  主要根据注释填写即可。要注意的是，在计算`stack_offset`的时候之所以要减去`0x1000`,是因为根据`chcore_pmo_write`函数参数可以知道初始化的`env`的大小为`0x1000`，所以栈要预先为这部分留出来空间。

  ```c
  int launch_process(struct launch_process_args *lp_args)
  {
          ......
          /* LAB 4 TODO BEGIN: create pmo for main_stack_cap */
          // the stack pm should be mapped directly.
          main_stack_cap = __chcore_sys_create_pmo(MAIN_THREAD_STACK_SIZE, PMO_ANONYM);
          // printf("[DEBUG] finish launch_process stage 1!\n");
          /* LAB 4 TODO END */
          ......
          /* LAB 4 TODO BEGIN: set stack_top and offset */
          stack_top = MAIN_THREAD_STACK_SIZE + MAIN_THREAD_STACK_BASE;
          offset = MAIN_THREAD_STACK_SIZE - 0x1000;
          // printf("[DEBUG] finish launch_process stage 2!\n");
          /* LAB 4 TODO END */
          ......
          /* LAB 4 TODO BEGIN: fill pmo_map_requests */
          pmo_map_requests[0].free_cap = 1;
          pmo_map_requests[0].perm = VM_READ | VM_WRITE;
          pmo_map_requests[0].pmo_cap = main_stack_cap;
          pmo_map_requests[0].addr = ROUND_DOWN(MAIN_THREAD_STACK_BASE, PAGE_SIZE);
          // printf("[DEBUG] finish launch_process stage 3!\n");
          /* LAB 4 TODO END */
  		......
          args.cap_group_cap = new_process_cap;
          /* LAB 4 TODO BEGIN: set the stack for main thread */
          // this stack <--> SP_EL0
          args.stack = stack_top - 0x1000;
          // printf("[DEBUG] finish launch_process stage 4!\n");
          /* LAB 4 TODO END */
          args.pc = pc;
          ......
  }
  ```

  

- 练习题 14：在`libchcore/src/ipc/ipc.c`与`kernel/ipc/connection.c`中实现了大多数`IPC`相关的代码，请根据注释完成其余代码。

- **回答14**：

  - `ipc`部分：

    ```c
    ......
    /* Lab4: Register IPC server */
    int ipc_register_server(server_handler server_handler)
    {
            ......
            /* LAB 4 TODO BEGIN: fill vm_config */
            vm_config.stack_base_addr = SERVER_STACK_BASE;
            vm_config.stack_size = SERVER_STACK_SIZE;
            vm_config.buf_base_addr = SERVER_BUF_BASE;
            vm_config.buf_size = SERVER_BUF_SIZE;
            /* LAB 4 TODO END */
            ......
    }
    
    /* Lab4: Register IPC client */
    struct ipc_struct *ipc_register_client(int server_thread_cap)
    {
            ......
            /* LAB 4 TODO BEGIN: fill vm_config according to client_id */
            // different client should have different shared buffer start addr.
            // u64 client_buf_start = CLIENT_BUF_BASE + client_id * CLIENT_BUF_SIZE;
            vm_config.buf_base_addr = CLIENT_BUF_BASE + client_id * CLIENT_BUF_SIZE;
            vm_config.buf_size = CLIENT_BUF_SIZE;
            /* LAB 4 TODO END */
            ......
    }
    ......
    int ipc_set_msg_data(struct ipc_msg *ipc_msg, void *data, u64 offset, u64 len)
    {
           ......
            /* LAB 4 TODO BEGIN */
            memcpy(ipc_get_msg_data(ipc_msg) + offset, data, len);
            /* LAB 4 TODO END */
            return 0;
    }
    ```

  - `connection`部分：

    ```c
    static int create_connection(struct thread *source, struct thread *target,
                                 struct ipc_vm_config *client_vm_config)
    {
            ......
            // Create the server thread's stack
            /* Lab4: set server_stack_base */
            /* LAB 4 TODO BEGIN */
            server_stack_base = vm_config->stack_base_addr + vm_config->stack_size * conn_idx;
            /* LAB 4 TODO END */
            ......
            /* LAB 4 TODO BEGIN */
            server_buf_base = vm_config->buf_base_addr + vm_config->buf_size * conn_idx;
            client_buf_base = client_vm_config->buf_base_addr;
            /* LAB 4 TODO END */
    		......
            /* LAB 4: map shared ipc buf to vmspace of server and client */
            /* LAB 4 TODO BEGIN */
            vmspace_map_range(target->vmspace, server_buf_base, buf_size, VMR_READ | VMR_WRITE, buf_pmo);
            vmspace_map_range(source->vmspace, client_buf_base, buf_size, VMR_READ | VMR_WRITE, buf_pmo);
            /* LAB 4 TODO END */
    		......
    }
    static u64 thread_migrate_to_server(struct ipc_connection *conn, u64 arg)
    {
            ......
            /* LAB 4 TODO BEGIN: use arch_set_thread_stack*/
            arch_set_thread_stack(target, conn->server_stack_top);
            /* LAB 4 TODO END */
            /* LAB 4 TODO BEGIN: use arch_set_thread_next_ip */
            arch_set_thread_next_ip(target, callback);
            /* LAB 4 TODO END */
    		......
            /* LAB 4 TODO BEGIN: use arch_set_thread_arg0/1 */
            arch_set_thread_arg0(target, arg);
            arch_set_thread_arg1(target, current_thread->cap_group->pid);
            /* LAB 4 TODO END */
    		......
    }
    static int thread_migrate_to_client(struct ipc_connection *conn, u64 ret_value)
    {
            ......
            /* LAB 4 TODO BEGIN: use arch_set_thread_return */
            arch_set_thread_return(source, ret_value);
            /* LAB 4 TODO END */
    		......
    }
    int ipc_send_cap(struct ipc_connection *conn)
    {
            ......
            for (i = 0; i < cap_slot_number; i++) {
                    u64 dest_cap;
    
                    /* Lab4: copy the cap to server and update the cap_buf */
                    /* LAB 4 TODO BEGIN */
                    dest_cap = cap_copy(current_cap_group, conn->target->cap_group, cap_buf[i]);
                    if (dest_cap >= 0) {
                            cap_buf[i] = dest_cap;
                    }
                    else {
                            goto out_free_cap;
                    }
                    /* LAB 4 TODO END */
            }
            ......
    }
    ......
    void sys_ipc_return(u64 ret, u64 cap_num)
    {
            ......
            /* Lab4: update the thread's state and sc */
            /* LAB 4 TODO BEGIN */
            conn->source->thread_ctx->state = TS_RUNNING;
            conn->source->thread_ctx->sc = current_thread->thread_ctx->sc;
            /* LAB 4 TODO END */
    		......
    }
    u64 sys_ipc_call(u32 conn_cap, struct ipc_msg *ipc_msg, u64 cap_num)
    {
    		......
            /* LAB 4 TODO BEGIN: use ipc_send_cap */
            if (ipc_msg > 0) {
                    r = ipc_send_cap(conn);
            }
            /* LAB 4 TODO END */
    		......
            /* LAB 4 TODO BEGIN: set arg */
            u64 off = (u64) ipc_msg - (conn->buf).client_user_addr;
           	arg = off + (conn->buf).server_user_addr;
            /* LAB 4 TODO END */
    		......
    }
    ```

    

- 练习题 15：`ChCore`在`kernel/semaphore/semaphore.h`中定义了内核信号量的结构体，并在`kernel/semaphore/semaphore.c`中提供了创建信号量`init_sem`与信号量对应`syscall`的处理函数。请补齐`wait_sem`操作与`signal_sem`操作。

- **回答15**：

  - `wait_sem`函数：

    如果当前`sem_count`已经为0，说明已经没有资源可以直接使用了，所以需要将当前线程翻入等待队列，并将对应线程状态设置为`TS_WAITING`进行阻塞等待，并需要重新调度一个新的线程去执行，并跳转到那个线程。如果`sem_count`不为0，说明还有资源可以直接使用，所以直接更新计数器数值即可。具体细节参考注释以及文档提示。

    ```c
    s32 wait_sem(struct semaphore *sem, bool is_block)
    {
            s32 ret = 0;
            /* LAB 4 TODO BEGIN */
            if (sem->sem_count == 0) {
                    if (is_block) {
                            sem->waiting_threads_count++;
                            list_add(&current_thread->sem_queue_node, &sem->waiting_threads);
                            current_thread->thread_ctx->state = TS_WAITING;
                            current_thread->thread_ctx->sc->budget = 0;
                            arch_set_thread_return(current_thread, 0);
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
    ```

  - `signal_sem`函数：

    首先检查等待队列中是否还有线程在阻塞，如果还有，说明有人正在等待新的资源，因而无需更新计数器，而是直接将这个线程唤醒继续执行即可(就是相当于把心空余出来的资源直接分配给这个刚刚被唤醒的线程)，如果已经没有正在等待的线程，直接更新计数器数值即可。具体细节剑注释以及文档。

    ```C
    s32 signal_sem(struct semaphore *sem)
    {
            /* LAB 4 TODO BEGIN */
            if (sem->waiting_threads_count == 0) {
                    sem->sem_count++;
                    return 0;
            } else {
                    sem->waiting_threads_count--;
                    struct thread* to_sched = list_entry(sem->waiting_threads.prev, struct thread, sem_queue_node);
                    list_del(&to_sched->sem_queue_node);
                    // to_sched->thread_ctx->state = TS_INTER;
                    sched_enqueue(to_sched);
            }
            /* LAB 4 TODO END */
            return 0;
    }
    ```

    

- 练习题 16：在`userland/apps/lab4/prodcons_impl.c`中实现`producer`和`consumer`。

- **回答16**：

  在`producer`准备生产新的`slot`之前先等待一个`empty_slot`,并在生产成功之后通知正在等待`filled_slot`的线程；`consumer`在准备消费之前先等待一个`filled_slot`，并在消费之后唤醒一个正在等待`empty_slot`的线程。

  ```c
  void *producer(void *arg)
  {
          int new_msg;
          int i = 0;
          while (i < PROD_ITEM_CNT) {
                  /* LAB 4 TODO BEGIN */
                  __chcore_sys_wait_sem(empty_slot, true);
                  /* LAB 4 TODO END */
                  new_msg = produce_new();
                  buffer_add_safe(new_msg);
                  /* LAB 4 TODO BEGIN */
                  __chcore_sys_signal_sem(filled_slot);
                  /* LAB 4 TODO END */
                  i++;
          }
          printf("ready to add 1 in producer func!\n");
          __sync_fetch_and_add(&global_exit, 1);
          return 0;
  }
  void *consumer(void *arg)
  {
          int cur_msg;
          int i = 0;
          while (i < COSM_ITEM_CNT) {
                  /* LAB 4 TODO BEGIN */
                  __chcore_sys_wait_sem(filled_slot, true);
                  /* LAB 4 TODO END */
                  cur_msg = buffer_remove_safe();
  
                  /* LAB 4 TODO BEGIN */
                  __chcore_sys_signal_sem(empty_slot);
                  /* LAB 4 TODO END */
                  consume_msg(cur_msg);
                  i++;
          }
          __sync_fetch_and_add(&global_exit, 1);
          return 0;
  }
  ```

  

- 练习题 17：请使用内核信号量实现阻塞互斥锁，在`userland/apps/lab4/mutex.c`中填上`lock`与`unlock`的代码。注意，这里不能使用提供的`spinlock`。

- **回答17**：

  互斥锁实际上就可以理解为资源量为1的信号量，所以基于信号量实现的互斥锁如下。需要注意的是，由于相当于信号量为1的变量，所以在`init`之后需要先`signal_sem`一次，来保证`sem`的初始数值为1。

  ```c
  void lock_init(struct lock *lock)
  {
          /* LAB 4 TODO BEGIN */
          // create a sem to implement this lock.
          lock->lock_sem = __chcore_sys_create_sem();
          unlock(lock);
          /* LAB 4 TODO END */
  }
  void lock(struct lock *lock)
  {
          /* LAB 4 TODO BEGIN */
          __chcore_sys_wait_sem(lock->lock_sem, true);
          /* LAB 4 TODO END */
  }
  void unlock(struct lock *lock)
  {
          /* LAB 4 TODO BEGIN */
          __chcore_sys_signal_sem(lock->lock_sem);
          /* LAB 4 TODO END */
  }
  ```

  