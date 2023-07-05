# `OS-Lab5`实验报告

- 思考题 1: 文件的数据块使用基数树的形式组织有什么好处? 除此之外还有其他的数据块存储方式吗?

- **回答1**：

  - 使用基数树组织数据块的好处：能够加快查找某一个具体数据块时候的性能。
  - 其他的数据块存储方式：可以使用传统`inode`结构来存储数据块，即维护一个了链表来表示一个`inode`的所有数据块。

- 练习题 2：实现位于`userland/servers/tmpfs/tmpfs.c`的`tfs_mknod`和`tfs_namex`。

- **回答2**：

  - `tfs_mknod`:

    ```c
    static int tfs_mknod(struct inode *dir, const char *name, size_t len, int mkdir)
    {
    	......
    	/* LAB 5 TODO BEGIN */
        // 首先根据mkdir是否为true来确定新的inode的具体类型。
    	if (mkdir == 0) {
    		inode = new_reg();
    	}
    	else {
    		inode = new_dir();
    	}
        //之后添加新的目录项
    	dent = new_dent(inode, name, len);
    	init_hlist_node(&dent->node);
        //之后将新的目录项添加到对应的父目录对应数据结构中(参考文档)
    	htable_add(&(dir->dentries), (u32)((dent->name).hash), &(dent->node));
    	dir->size++;
    	/* LAB 5 TODO END */
    	return 0;
    }
    ```

  - `tfs_namex`:

    ```c
    int tfs_namex(struct inode **dirat, const char **name, int mkdir_p)
    {
    	......
    	/* LAB 5 TODO BEGIN */
    	i = 0;
        // 使用while(true)是希望通过类似于递归的方式来逐层的去找到对应文件的父目录。
    	while(true) {
    		curName = *name;
            // 这个循环是在找一个子路径，即当前层的文件(目录),并存入buf中
            // /a/b/c --> get a
    		while (*curName != '\0' && *curName != '/') {
    			curName++;
    			i++;
    		}
            //死循环(递归)结束条件
    		if (*curName == '\0') {
    			return 0;
    		}
    		memcpy(buff, *name, i);
            //小心!
    		buff[i] = '\0';
            //注意tfs_lookup函数作用范围ls指令相同
    		dent = tfs_lookup(*dirat, buff, i);
    		if (dent == NULL) {
    			if (mkdir_p != 0) {
    				tfs_mkdir(*dirat, buff, i);
    				dent = tfs_lookup(*dirat, buff, i);
    			} else {
    				return -ENONET;
    			}
    		}
            //更新循环条件(相当于进行了一次递归调用)
    		*dirat = dent->inode;
    		*name = curName + 1;
    		i = 0;
    	}
    	/* LAB 5 TODO END */
    	return 0;
    }
    ```

- 练习题 3：实现位于`userland/servers/tmpfs/tmpfs.c`的`tfs_file_read`和`tfs_file_write`。

- **回答3**：

  - `tfs_file_read`:

    ```c
    ssize_t tfs_file_read(struct inode * inode, off_t offset, char *buff,
    		      size_t size)
    {
    	......
    	/* LAB 5 TODO BEGIN */
        //目标是读取size这么多的字节数据，但是还要考虑文件可能会提前结束(read不能拓展文件)
    	while (cur_off <= inode->size && size > 0) {
            //计算索引，用于进行基数树查找
    		page_no = cur_off / PAGE_SIZE;
    		page_off = cur_off  - PAGE_SIZE * page_no;
    		page = radix_get(&(inode->data), page_no);
    		u64 left = PAGE_SIZE - page_off;
            //获取当次要读取的数据的数量，一次最多读取一个PAGE_SIZE.
    		if (left >= size) {
    			to_read = size;
    		}
    		else {
    			to_read = left;
    		}
            //在希望进行char数据复制的时候，memcpy是好用的，如果自己手动复制，记得\0的问题！
    		if (page != NULL) memcpy(buff, page + page_off, to_read);
    		else memcpy(buff, 0, to_read);
    		buff += to_read;
    		size -= to_read;
    		cur_off += to_read;
    	}
    	/* LAB 5 TODO END */
    	return cur_off - offset;
    }
    ```

  - `tfs_file_write`:

    ```c
    ssize_t tfs_file_write(struct inode * inode, off_t offset, const char *data,
    		       size_t size)
    {
    	......
    	/* LAB 5 TODO BEGIN */
    	u64 curWrite = 0;
        //write函数只需要考虑要写入的实际数据量，因为write允许文件的扩容。
    	while (size > 0) {
    		page_no = cur_off / PAGE_SIZE;
    		page_off = cur_off - PAGE_SIZE * page_no;
    		page = radix_get(&(inode->data), page_no);
    		u64 left = PAGE_SIZE - page_off;
            //每次最多写入一个PAGE_SIZE
    		if (left <= size) {
    			to_write = left;
    		} 
    		else {
    			to_write = size;
    		}
            //超过了文件原有大小，进行扩容。
    		if (page == NULL) {
    			page = malloc(PAGE_SIZE);
    			radix_add(&(inode->data), page_no, page);
    		}
    		memcpy(page + page_off, data + curWrite, to_write);
    		curWrite += to_write;
    		cur_off += to_write;
    		size -= to_write;
    	}
        //由于write允许文件大小更新，所以要记得在最后检查文件大小是否改变(之前忘记了出现BUG)
    	if (cur_off >= inode->size) {
    		inode->size = cur_off;
    	}
    	/* LAB 5 TODO END */
    	return cur_off - offset;
    }
    ```

- 练习题 4：实现位于`userland/servers/tmpfs/tmpfs.c`的`tfs_load_image`函数。

- **回答4**：

  ```c
  int tfs_load_image(const char *start)
  {
  	......
  	for (f = g_files.head.next; f; f = f->next) {
  		/* LAB 5 TODO BEGIN */
  		curName = f->name;
  		dirat = tmpfs_root_dent->inode;
          // 助理的namex函数的作用是通过类似递归的方式找到最近父目录，所以每一个文件的查找都从根目录开始
  		tfs_namex(&dirat, &curName, 1);
  		len = strlen(curName);
  		dent = tfs_lookup(dirat, curName, len);
  		if (dent == NULL) {
              //如果对应位置为空就要重新创建，因为最终目的是要倒入所有文件
  			tfs_creat(dirat, curName, len);
  			dent = tfs_lookup(dirat, curName, len);
  			write_count = tfs_file_write(dent->inode, 0, f->data, f->header.c_filesize);
  		}
  		else {
  			continue;
  		}
  		/* LAB 5 TODO END */
  	}
  	return 0;
  }
  ```

- 练习题 5：利用`userland/servers/tmpfs/tmpfs.c`中已经实现的函数，完成在`userland/servers/tmpfs/tmpfs_ops.c`中的`fs_creat`、`tmpfs_unlink`和`tmpfs_mkdir`函数，从而使`tmpfs_*`函数可以被`fs_server_dispatch`调用以提供系统服务。

- **回答5**：本问题主要思路就是先使用`tfs_namex`函数找到对应的位置，之后进行相应的操作。

  - `fs_creat`:

    ```C
    	......
    	/* LAB 5 TODO BEGIN */
    	dirat = tmpfs_root_dent->inode;
    	err = tfs_namex(&dirat, &leaf, 1);
    	// leaf不为空串说明该文件不存在(如果存在应该将整个路径都对应的清除掉)
    	if (*leaf != '\0') {
    		err = tfs_creat(dirat, leaf, strlen(leaf));
    	}
    	/* LAB 5 TODO END */
    	return 0;
    ```

  - `tmpfs_unlink`：

    ```C
    int tmpfs_unlink(const char *path, int flags)
    {
    	......
    	/* LAB 5 TODO BEGIN */
    	dirat = tmpfs_root_dent->inode;
    	err = tfs_namex(&dirat, &leaf, 0);
        // 注意这时候通过namex函数找对应文件的时候，不能将最后一个参数设置为1(出现了bug，因为会创建本来不存在的文件)
    	if (!err) {
    		err = tfs_remove(dirat, leaf, strlen(leaf));
    	}
    	/* LAB 5 TODO END */
    	return err;
    }
    ```

  - `tmpfs_mkdir`:

    ```C
    int tmpfs_mkdir(const char *path, mode_t mode)
    {
    	......
    	/* LAB 5 TODO BEGIN */
    	dirat = tmpfs_root;
    	err = tfs_namex(&dirat, &leaf, 1);
    	if (!err) {
    		err = tfs_mkdir(dirat, leaf, strlen(leaf));
    	}
    	/* LAB 5 TODO END */
    	return err;
    }
    ```

- 练习题 6：补全`libchcore/src/libc/fs.c`与`libchcore/include/libc/FILE.h`文件，以实现`fopen`, `fwrite`, `fread`, `fclose`, `fscanf`, `fprintf`五个函数，函数用法应与`libc`中一致。

- **回答6**：本部分主要是对于`LAB4`中`IPC`相关操作的封装。然后根据不同的目标分别进行不同的处理。具体的`IPC`用法可以参考`read_file_from_tfs()`函数的具体实现。

  - `FILE`定义：

    ```c
    typedef struct FILE {
        int fd;//file descriptor
        int off;//当前文件的读写指针位置
    } FILE;
    ```

  - `fopen`:

    ```c
    FILE *fopen(const char * filename, const char * mode) {
    
    	/* LAB 5 TODO BEGIN */
    	if (fs_ipc_struct == NULL) {
    		connect_tmpfs_server_self();
    	}
    	int file_fd = 1;
    	struct ipc_msg* ipc_msg = ipc_create_msg(fs_ipc_struct, sizeof(struct fs_request), 0);
    	chcore_assert(ipc_msg);
    	struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(ipc_msg);
    	fr->req = FS_REQ_OPEN;
    	strcpy(fr->open.pathname, filename);
    	fr->open.flags = O_RDONLY;
    	fr->open.new_fd = file_fd;
        // 注意因为不一定是根目录，所以需要重新赋值fd
    	file_fd = ipc_call(fs_ipc_struct, ipc_msg);
    	ipc_destroy_msg(fs_ipc_struct, ipc_msg);
        if (file_fd < 0) {
            //目标文件不存在，那么要先创建
    		struct ipc_msg* ipc_msg = ipc_create_msg(fs_ipc_struct, sizeof(struct fs_request), 0);
    		chcore_assert(ipc_msg);
    		struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(ipc_msg);
    		fr->req = FS_REQ_CREAT;
    		fr->creat.mode = getFileMode(mode);
    		strcpy(fr->creat.pathname, filename);
    		ipc_call(fs_ipc_struct, ipc_msg);
    		ipc_destroy_msg(fs_ipc_struct, ipc_msg);
    		struct ipc_msg* ipc_msg_1 = ipc_create_msg(fs_ipc_struct, sizeof(struct fs_request), 0);
    		chcore_assert(ipc_msg_1);
            //注意创建完成新的文件之后需要重新打开新的文件
    		struct fs_request* fr1 = (struct fs_request*)ipc_get_msg_data(ipc_msg_1);
    		fr1->req = FS_REQ_OPEN;
    		strcpy(fr1->open.pathname, filename);
    		fr1->open.flags = O_RDONLY;
    		fr1->open.new_fd = 1;
            //同样的需要重新赋值fd
    		file_fd = ipc_call(fs_ipc_struct, ipc_msg_1);
    		ipc_destroy_msg(fs_ipc_struct, ipc_msg_1);
    	}
        //之后创建相应结构体
    	FILE* f = malloc(sizeof(FILE));
    	f->fd = file_fd;
    	f->off = 0;
    	/* LAB 5 TODO END */
        return f;
    }
    ```

  - `fwrite`:(**注：从此开始，相似的`IPC`操作不再列出**)

    ```c
    size_t fwrite(const void * src, size_t size, size_t nmemb, FILE * f) {
    
    	/* LAB 5 TODO BEGIN */
    	......
        //首先通过IPC LSEEK来定位写入的开始位置
    	fr->req = FS_REQ_LSEEK;
    	fr->lseek.fd = f->fd;
    	fr->lseek.offset = f->off;
    	fr->lseek.whence = 0; // ???
    	......
    	//注意：由于write的时候需要将write的内容用IPC一起发送，所以要增大原始IPC_MSG的大小
        size_t totalSize = sizeof(struct fs_request) + nmemb + 1;
        //通过一个write_IPC将数据真正写入
    	struct ipc_msg* write_ipc_msg = ipc_create_msg(fs_ipc_struct, totalSize, 0);
    	......
    	write_fr->req = FS_REQ_WRITE;
    	write_fr->write.fd = f->fd;
    	write_fr->write.count = nmemb;
    	.......
        //注意更新文件的读写指针位置！
    	f->off += nmemb;
    	//通过IPC关闭文件
        ......
    	/* LAB 5 TODO END */
        return ret;
    }
    ```

  - `fread`:

    ```c
    size_t fread(void * destv, size_t size, size_t nmemb, FILE * f) {
    
    	/* LAB 5 TODO BEGIN */
    	int ret;
    	//先使用LSEEK IPC来定位初始位置
    	fr->req = FS_REQ_LSEEK;
    	fr->lseek.fd = f->fd;
    	fr->lseek.offset = f->off;
    	fr->lseek.whence = 0; // ???
    	......
    	//通过一个read IPC读取数据
    	fr1->req = FS_REQ_READ;
    	fr1->read.fd = f->fd;
    	fr1->read.count = nmemb * size;
    	ret = ipc_call(fs_ipc_struct, read_ipc_msg);
    	if (ret > 0) {
            //注意！！要记得把结果copy到目标数组中(最开始忘了，读不出来数据)
    		char* gotData = ipc_get_msg_data(read_ipc_msg);
    		memcpy(destv, gotData, ret);
    		f->off += nmemb * size;
    	}
    	......
    	/* LAB 5 TODO END */
        return ret;
    }
    ```

  - `fclose`:

    ```c
    int fclose(FILE *f) {
    	/* LAB 5 TODO BEGIN */
        //直接使用一个close IPC解决
    	struct ipc_msg* close_ipc_msg = ipc_create_msg(fs_ipc_struct, sizeof(struct fs_request), 0);
    	chcore_assert(close_ipc_msg);
    	struct fs_request* fr = (struct fs_request*) ipc_get_msg_data(close_ipc_msg);
    	fr->req = FS_REQ_CLOSE;
    	fr->close.fd = f->fd;
    	ipc_call(fs_ipc_struct, close_ipc_msg);
    	ipc_destroy_msg(fs_ipc_struct, close_ipc_msg);
    	/* LAB 5 TODO END */
        return 0;
    }
    ```

  - `fscanf`:(这个函数在实现的时候，面向测试编程了。。。如数组长度8192等。)

    ```C
    int fscanf(FILE * f, const char * fmt, ...) {
    	/* LAB 5 TODO BEGIN */
    	//一些初始化操作
        ......
    	while (*arg != '\0') {
    		if (startFlag == false) {
    			if (*arg == '%') {
                    //用于表示下一位是不是一个占位符
    				startFlag = true;
    			}
    			else {
    				//一般字符情况
                    ......
    			}
    		}
    		else {
    			startFlag = false;
    			if (*arg == 'd') {
    				//%d的用法
                    ......
    			}
    			if (*arg == 's') {
    				//%s的情况
                    ......
    			}
    		}
    		arg++;
    	}
    	/* LAB 5 TODO END */
        return 0;
    }
    ```

  - `fprintf`:

    ```C
    int fprintf(FILE * f, const char * fmt, ...) {
    	//一些初始化操作
        ......
    	while (*arg != '\0') {
    		char c = *arg;
    		if (startFlag == false) {
    			if (c == '%') {
                    //用于表示下一位是不是一个占位符
    				startFlag = true;
    			}
    			else {
                    //注意在拼接两个字符串的时候，要记得在结尾加"\0"！！！
    				//一般情况
                    ......
    			}
    		}
    		else {
    			startFlag = false;
    			if (c == 'd') {
    				//%d情况
                    ......
    			}
    			if (c == 's') {
    				//%s情况
                    ......
    			}
    		}
    		arg++;
    	}
    	va_end(args);
        //和前面的fread类似，要把最终数据落实到目标位置(最开始也忘了)
    	int res = fwrite(data, sizeof(char), strlen(data), f);
    	/* LAB 5 TODO END */
        return res;
    }
    ```

- 练习题 7：实现在`userland/servers/shell/main.c`中定义的`getch`，该函数会每次从标准输入中获取字符，并实现在`userland/servers/shell/shell.c`中的`readline`，该函数会将按下回车键之前的输入内容存入内存缓冲区。代码中可以使用在`libchcore/include/libc/stdio.h`中的定义的I/O函数。

- **回答7**：

  - `getch`:直接调用相关`syscall`即可。

  - `readline`:在不考虑`do_complement`的时候，只需要考虑一般字符以及换行符(结束)的情况。

    ```c
    char *readline(const char *prompt)
    {
    	......
    	/* LAB 5 TODO BEGIN */
    	if (c == '\r' || c == '\n') {
    		chcore_console_putc('\r');
    		chcore_console_putc('\n');
    		buf[i] = '\0';
    		return buf;
    	}
    	else {
    		if (c != '\t') {
    			buf[i] = c;
    			i++;
    			chcore_console_putc(c);
    		}
    	}
    	/* LAB 5 TODO END */
    	}
    	buf[i] = '\0';
    
    	return buf;
    }
    ```

- 练习题 8：根据在`userland/servers/shell/shell.c`中实现好的`bultin_cmd`函数，完成shell中内置命令对应的`do_*`函数，需要支持的命令包括：`ls [dir]`、`echo [string]`、`cat [filename]`和`top`。

- **回答8**：

  - `ls`:需要实现`fs_scan`函数。思路是直接找到父目录位置，使用提供的`getdents`函数读取每一个目录项。注意要跳过`.`和`..`。

    ```c
    void fs_scan(char *path)
    {
    	/* LAB 5 TODO BEGIN */
    	......
        //这里没有直接使用fopen,是因为fopen会在文件不存在的时候直接创建，而ls是不能作用于不存在文件的。
    	fr->req = FS_REQ_OPEN;
    	int file_fd = 1;
    	fr->open.new_fd = file_fd;
    	......
    	//复用getdents
    	int ret = getdents(file_fd, scan_buf, BUFLEN);
    	int offset;
    	struct dirent* p;
    	for (offset = 0; offset < ret; offset += p->d_reclen) {
    		p = (struct dirent *)(scan_buf + offset);
    		get_dent_name(p, name);
            //跳过.和..两个link 目录。
    		if (strlen(name) == 1 && name[0] == '.') {
    			continue;
    		}
    		if (strlen(name) == 2 && name[0] == '.' && name[1] == '.') {
    			continue;
    		}
    		printf("%s ", name);
    	}
    	/* LAB 5 TODO END */
    }
    ```

  - `echo`:直接调用`chcore_console_putc`的`syscall`。

    ```c
    int do_echo(char *cmdline)
    {
    	/* LAB 5 TODO BEGIN */
    	chcore_console_printf("%s\n", cmdline);
    	/* LAB 5 TODO END */
    	return 0;
    }
    ```

  - `cat`:需要实现`print_content`函数。思路是先找到文件位置，然后使用`fread`读取，最后`fclose`关闭。

    ```c
    void print_file_content(char* path) 
    {
    
    	/* LAB 5 TODO BEGIN */
        //打开文件
    	FILE* f = fopen(path, "r");
    	int ret;
    	do {
    		char buf[257];
            //读取数据
    		ret = fread(buf, sizeof(char), 256, f);
    		chcore_console_printf("%s", buf);
    	} while (ret > 0);
        //关闭文件
    	fclose(f);
    	/* LAB 5 TODO END */
    }
    ```

  - `top`:已经提供了实现好了的版本。

- 练习题 9：实现在`userland/servers/shell/shell.c`中定义的`run_cmd`，以通过输入文件名来运行可执行文件，同时补全`do_complement`函数并修改`readline`函数，以支持按tab键自动补全根目录（`/`）下的文件名。

- **回答9**：

  - `do_complement`:此函数的关键是理解所谓`complement`是什么意思以及`complement_time`的具体含义。通过使用`qemu`进行调试可知，`complement`实际上就是在用户输入"\t"字符之后，直接将输出**替换成**一个根目录下的文件名，并且会以轮循的方式循环。所以，`complement_time`的含义就是某一次要替换成的文件条目的**在不考虑`.`和`..`的情况下的**在根目录`ls`输出内容中的索引。

    ```c
    int do_complement(char *buf, char *complement, int complement_time)
    {
    	......
    	/* LAB 5 TODO BEGIN */
    	......
    	fr->req = FS_REQ_OPEN;
    	int file_fd = 1;
    	fr->open.new_fd = file_fd;
    	......
    	ret = getdents(file_fd, scan_buf, BUFLEN);
    	for (offset = 0; offset < ret; offset += p->d_reclen) {
    		p = (struct dirent *)(scan_buf + offset);
    		get_dent_name(p, name);
    		if (strcmp(name, ".") == 0) {
    			// ".",实际上既然是根目录，也可以直接不考虑".."。
    			complement_time++;
    		} else {
    			if (j == complement_time) {
    				strcpy(complement, name);
    				return 0;
    			}
    		}
    		j++;
    	}
    	/* LAB 5 TODO END */
    	return r;
    }
    ```

  - 修改之后的`readline`:只展示了相对于上一个问题新增加的部分内容。

    ```c
    char *readline(const char *prompt)
    {
    	......
    	/* LAB 5 TODO BEGIN */
    	......
    		else {
    			char complement_buf[8192];
    			ret = do_complement(complement_buf, complement, complement_time);
    			if (ret == 0) {
    				for (int k = 0; k < i; ++k) {
                        //含义是将前一位变成空格
    					chcore_console_putc('\b');
    					chcore_console_putc(' ');
    					chcore_console_putc('\b');
    				}
    				chcore_console_printf("%s", complement);
    				complement_time++;
    				i = strlen(complement);
    			}
    			else {
    				complement_time = 0;
    			}
    		}
    	}
    	/* LAB 5 TODO END */
    	}
    	buf[i] = '\0';
    	return buf;
    }
    ```

- 练习题 10：`FSM`需要两种不同的文件系统才能体现其特点，本实验提供了一个`fakefs`用于模拟部分文件系统的接口，测试代码会默认将`tmpfs`挂载到路径`/`，并将`fakefs`挂载在到路径`/fakefs`。本练习需要实现`userland/server/fsm/main.c`中空缺的部分，使得用户程序将文件系统请求发送给`FSM`后，`FSM`根据访问路径向对应文件系统发起请求，并将结果返回给用户程序。实现过程中可以使用`userland/server/fsm`目录下已经实现的函数。

- **回答10**：

  - `FS_REQ_OPEN`:(`FS_REQ_MKDIR/RMDIR/UNLINK/CREAT`与此几乎相同)
  
    ```c
    		case FS_REQ_OPEN:
    			//第一种情况，提供了目标路径，通过get_mount_point函数来获取文件系统信息
    			mpinfo = get_mount_point(fr->open.pathname, strlen(fr->open.pathname));
    			strip_path(mpinfo, fr->open.pathname);
    			fsm_set_mount_info_withfd(client_badge, fr->open.new_fd, mpinfo);
    			ipc_msg_1 = ipc_create_msg(mpinfo->_fs_ipc_struct, sizeof(struct fs_request), 0);
    			fr_1 = (struct fs_request*)ipc_get_msg_data(ipc_msg_1);
    			fr_1->req = FS_REQ_OPEN;
    			fr_1->open.new_fd = fr->open.new_fd;
    			strcpy(fr_1->open.pathname, fr->open.pathname);
    			ret = ipc_call(mpinfo->_fs_ipc_struct, ipc_msg_1);
    			ipc_destroy_msg(mpinfo->_fs_ipc_struct, ipc_msg_1);
    ```
  
  - `FS_REQ_CLOSE`:
  
    ```c
    		case FS_REQ_CLOSE:
    			//第二种情况，没有路径，只有fd，通过fsm_get_mount_info_withfd获取信息
    			mpinfo = fsm_get_mount_info_withfd(client_badge, fr->close.fd);
    			ipc_msg_1 = ipc_create_msg(mpinfo->_fs_ipc_struct, sizeof(struct fs_request), 0);
    			fr_1 = (struct fs_request*)ipc_get_msg_data(ipc_msg_1);
    			fr_1->req = FS_REQ_CLOSE;
    			fr_1->close.fd = fr->close.fd;
    			// strcpy(fr_1->open.pathname, fr->open.pathname);
    			ret = ipc_call(mpinfo->_fs_ipc_struct, ipc_msg_1);
    			ipc_destroy_msg(mpinfo->_fs_ipc_struct, ipc_msg_1);
    ```
  
  - `FS_REQ_READ`:
  
    ```c
    		case FS_REQ_READ:
    			mpinfo = fsm_get_mount_info_withfd(client_badge, fr->read.fd);
    			ipc_msg_1 = ipc_create_msg(mpinfo->_fs_ipc_struct, sizeof(struct fs_request), 0);
    			fr_1 = (struct fs_request*)ipc_get_msg_data(ipc_msg_1);
    			fr_1->read.fd = fr->read.fd;
    			fr_1->read.count = fr->read.count;
    			fr_1->req = FS_REQ_READ;
    			ret = ipc_call(mpinfo->_fs_ipc_struct, ipc_msg_1);
    			// 注意数据要拷贝到目标位置!!!!
    			memcpy(ipc_get_msg_data(ipc_msg), ipc_get_msg_data(ipc_msg_1), ret);
    			ipc_destroy_msg(mpinfo->_fs_ipc_struct, ipc_msg_1);
    ```
  
  - `FS_REQ_WRITE:`
  
    ```c
    		case FS_REQ_WRITE:
    			mpinfo = fsm_get_mount_info_withfd(client_badge, fr->write.fd);
    			ipc_msg_1 = ipc_create_msg(mpinfo->_fs_ipc_struct, sizeof(struct fs_request) + fr->write.count + 1, 0);
    			fr_1 = (struct fs_request*)ipc_get_msg_data(ipc_msg_1);
    			fr_1->write.fd = fr->write.fd;
    			fr_1->write.count = fr->write.count;
    			fr_1->req = FS_REQ_WRITE;
    			// NOTE: how to set fr_1->write.write_buff_begin here ??
    			fr_1->write.write_buff_begin = fr->write.write_buff_begin;
    			ret = ipc_call(mpinfo->_fs_ipc_struct, ipc_msg_1);
    			ipc_destroy_msg(mpinfo->_fs_ipc_struct, ipc_msg_1);
    			break;
    ```
  
  - `FS_REQ_LSEEK/GET_SIZE`:(没有做什么事情)
  
    ```c
    		case FS_REQ_GET_SIZE:
    			break;
    		case FS_REQ_LSEEK:
    			break;
    ```
  
  - `FS_REQ_GETDENTS64`:此种情况实现可以参考`getdents`函数。
  
    ```c
    case FS_REQ_GETDENTS64:
    			// see getdents function as an example.
    			mpinfo = fsm_get_mount_info_withfd(client_badge, fr->getdents64.fd);
    			ipc_msg_1 = ipc_create_msg(mpinfo->_fs_ipc_struct, sizeof(struct fs_request), 0);
    			fr_1 = (struct fs_request*)ipc_get_msg_data(ipc_msg_1);
    			fr_1->getdents64.fd = fr->getdents64.fd;
    			fr_1->getdents64.count = fr->getdents64.count;
    			fr_1->req = FS_REQ_GETDENTS64;
    			ret = ipc_call(mpinfo->_fs_ipc_struct, ipc_msg_1);
    			memcpy(ipc_get_msg_data(ipc_msg), ipc_get_msg_data(ipc_msg_1), ret);
    			ipc_destroy_msg(mpinfo->_fs_ipc_struct, ipc_msg_1);
    			break;
    ```
  
    