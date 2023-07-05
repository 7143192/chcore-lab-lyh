# `LAB6`实验报告

**思考题1**：请自行查阅资料，并阅读`userland/servers/sd`中的代码，回答以下问题:

- `circle`中还提供了`SDHost`的代码。`SD`卡，`EMMC`和`SDHost`三者之间的关系是怎么样的？

  **回答1**：

  SD卡和`EMMC`是存储介质，`SDHost`是它们与主机设备之间的接口标准。具体而言：

  - SD卡是一种存储介质，它是一种可拆卸的闪存卡片，用于存储数据和程序的。SD卡直接通常被应用于移动设备，如手机、相机和`MP3`等，以便扩展存储容量。
  - `EMMC`是一种内置式存储介质，它是一种集成电路，用于移动设备上的固态存储。它可以看作是一个内置的SD卡，但用户无法拆卸。`EMMC`通常被用于移动设备，在跟SD卡相比，它具有更高的读写速度和更大的存储容量。
  - `SDHost`是一个硬件接口标准，它是一个SD卡或`EMMC`与主机设备之间的接口，可以进行数据传输和控制信号的交互。所有支持SD卡或`EMMC`的主机设备都必须遵循`SDHost`标准，才能与这些卡片进行通信。

  

- 请**详细**描述`Chcore`是如何与`SD`卡进行交互的？即`Chcore`发出的指令是如何输送到`SD`卡上，又是如何得到`SD`卡的响应的。(提示: `IO`设备常使用`MMIO`的方式映射到内存空间当中)

  **回答2**：

  **如何输送**：`SD`卡端会以`sd_dispatch`函数为启动函数的`IPC Server`,之后`chcore`通过`IPC`的方式(具体种类为`SD_IPC_REQ_READ/SD_IPC_REQ_WRITE`)将请求发送到这个`IPC Server`上面，之后`Server`分别通过`sdcard_readblock/sdcard_writeblock`函数来具体的处理相应请求。

  **如何响应**：`SD Server`在接受到响应请求之后，会依次调用`eMMC Driver`中的`DoRead/DoWrite,DoDataCommand,IssueCommand(Int)`函数来处理响应请求。由于使用了`MMIO`的方式来访问外设，所以实际上的读写流程就是对于相关的`I/O Registers`进行操作。同时在进行操作的过程中，通过`busy`状态的判断以及阻塞机制来保证在同时需要操作多个`block`的时候是按照一定顺序完成的。之后将修改之后的寄存器通过发送相关的`CMD`返回到上层，最终将数据返回给用户。

  

- 请简要介绍一下`SD`卡驱动的初始化流程。

  **回答3**：

  1. `GPIO`初始化：根据SD卡引脚配置，初始化与SD卡相关联的`GPIO`管脚，用于控制SD卡的插拔、上下电等。
  2. 时钟初始化：SD卡与主机之间的通信需要一个时钟源，需要根据SD卡的时钟频率配置主机的时钟源。
  3. `SDHost`控制器初始化：配置`SDHost`控制器的工作模式、总线宽度、寄存器等。
  4. SD卡复位：先将SD卡初始化为`IDLE`状态，以便后续进行读取操作。
  5. SD卡初始化：发送`CMD0`命令将SD卡初始化为SPI模式或SD模式，发送`CMD8`命令检测SD卡的性能等级，发送`ACMD41`命令使SD卡处于就绪状态。
  6. `CID/CSD`寄存器读取：读取`CID`和`CSD`寄存器中的信息，包括SD卡制造商、型号、容量、读写速度等参数。
  7. 设置SD卡工作模式：根据SD卡的类型和参数，配置`SDHost`控制器的相关参数，如时序、电压等。
  8. 进入传输状态：发送`CMD7`命令进入传输状态，处于传输状态后可以进行数据的读写操作。



- 在驱动代码的初始化当中，设置时钟频率的意义是什么？为什么需要调用`TimeoutWait`进行等待?

  **回答4**：

  - **设置时钟频率的意义**：设置正确的时钟频率可以确保SD卡与主机之间的数据传输速度和稳定性，提高文件读写效率和系统性能。
  - **为何需要`TimeOutWait`的等待**：
    - 因为通过`SD`卡进行读写操作的时候可能超时，设置超时等待可以有效避免这种情况带来的影响。
    - `SD`卡本身对于请求的响应不稳定，通过设置超时机制，可以提升系统的稳定性。

  

**练习1**：完成`userland/servers/sd`中的代码，实现SD卡驱动。驱动程序需实现为用户态系统服务，应用程序与驱动通过`IPC`通信。需要实现`sdcard_readblock`与`sdcard_writeblock`接口，通过`Logical Block` `Address`(`LBA`) 作为参数访问`SD`卡的块。

**回答**：

`emmc.c/h`中的逻辑主要参考提供的`circle`项目的代码逻辑进行的实现，两者几乎相同，所以此处不在展示。

对于`sdcard_readblock/sdcard_writeblock`接口，实现如下：

```c
static int sdcard_readblock(int lba, char *buffer)
{
    //使用SEEK函数的逻辑进行地址的定位(emmc的逻辑是通过维护一个全局的ulloffset变量来确定起始地址)
	Seek(lba * BLOCK_SIZE);
    //每次读取一个BLOCK的数据。所以在后续实现naive_fs的时候需要注意一次读/写不到一个BLOCK_SIZE的边界情况处理。
	int ret = sd_Read(buffer, BLOCK_SIZE);
	if (ret < 0) return -1;
	return 0;
}

static int sdcard_writeblock(int lba, const char *buffer)
{
    //思路同上
	Seek(lba * BLOCK_SIZE);
	int ret = sd_Write(buffer, BLOCK_SIZE);
	if (ret < 0) return -1;
	return 0;
}
```



**练习2**：实现`naive_fs`。

**回答**：

由于不需要考虑多层文件结构，所以使用一个**全局数组**来记录所有的文件信息，每一个文件信息使用如下数据结构存储：(由于`naive_fs`相关接口直接使用了`name`作为参数，所以直接在元数据中存储文件名了，但是实际上文件名不应该是元数据的一部分。)

```C
struct naiveNode {
    char* name;
    int id;
    int size;
    int offset;
};
//naiveNode的id与其在数组nodes中的index相同。
struct naiveNode* nodes[1024];
```

对于相关接口的实现，其实现逻辑实际上与`LAB5`中的文件系统接口逻辑类似。具体如下：

```c
// 用于记录当前已经创建了的还没有被删除的文件数量
int cur = 0;
//用于将当前的nodes信息写入sd卡中。
//这里是直接将原数据序列化之后写入SD卡的第一个block。
void MarshalMetadata()
{
    char* buf = (char*) malloc(BLOCK_SIZE + 1);
    for (int i = 0; i <= BLOCK_SIZE; ++i) {
        buf[i] = 0;
    }
    int pos = 0;
    char* curStr = (char*) &cur;
    for (int i = 0; i < 4; ++i) {
        buf[pos++] = curStr[i];
    }
    for (int i = 0; i < cur; ++i) {
        int len = strlen(nodes[i]->name);
        char* lenStr = (char*) &len;
        for (int j = 0; j < 4; ++j) {
            buf[pos++] = lenStr[j];
        }
        for (int j = 0; j < len; ++j) {
            buf[pos++] = nodes[i]->name[j];
        }
        char* idStr = (char*) &(nodes[i]->id);
        for (int j = 0; j < 4; ++j) {
            buf[pos++] = idStr[j];
        }
        char* sizeStr = (char*) &(nodes[i]->size);
        for (int j = 0; j < 4; ++j) {
            buf[pos++] = sizeStr[j];
        }
        char* offStr = (char*) &(nodes[i]->offset);
        for (int j = 0; j < 4; ++j) {
            buf[pos++] = offStr[j];
        }
    }
    sd_bwrite(0, buf);
}
//从SD卡的第一个block中将数据读取出来。并将其恢复到内存数组nodes中去。
void UnmarshalMetadata()
{
    char* buf = (char*) malloc(BLOCK_SIZE + 1);
    for (int i = 0; i <= BLOCK_SIZE; ++i) {
        buf[i] = 0;
    }
    sd_bread(0, buf);
    int curNum = *(int*)buf;
    buf += sizeof(int);
    cur = curNum;
    for (int i = 0; i < cur; ++i) {
        nodes[i] = (struct naiveNode*) malloc(sizeof(struct naiveNode));
        int len = *(int*) buf;
        buf += 4;
        nodes[i]->name = (char*) malloc(len + 1);
        strncpy(nodes[i]->name, buf, len);
        nodes[i]->name[len] = '\0';
        buf += len;
        int id = *(int*) buf;
        buf += 4;
        nodes[i]->id = id;
        int size = *(int*) buf;
        buf += 4;
        nodes[i]->size = size;
        int off = *(int*) buf;
        buf += 4;
        nodes[i]->offset = off;
    }
    free(buf);
}

int naive_fs_access(const char *name)
{
	// 先从SD卡中读取已有文件信息
    UnmarshalMetadata();
    //之后遍历数组检查文件名是否存在
    for (int i = 0; i <= cur; ++i) {
        if (nodes[i] == NULL) {
            continue;
        }
        if (strcmp(nodes[i]->name, name) == 0) {
            return 0;
        }
    }
    return -1;
}

int naive_fs_creat(const char *name)
{
    //首先确保文件不存在
    if (naive_fs_access(name) == 0) {
        return -1;
    }
    // 在对应的索引位置创建新的文件
    struct naiveNode* node = (struct naiveNode*) malloc(sizeof(struct naiveNode));
    node->offset = 0;
    node->size = 0;
    node->name = (char*) malloc(strlen(name) + 1);
    strcpy(node->name, name);
    nodes[cur] = node;
    node->id = cur + 1;
    cur++;
	// 因为修改了文件系统信息，所以要重新写入SD卡来保存元数据信息
    MarshalMetadata();
    return 0;
}

int naive_fs_pread(const char *name, int offset, int size, char *buffer)
{
    //首先要确保文件已经存在
    if (naive_fs_access(name) != 0) {
        return -1;
    }
    int idx = -1;
    for (int i = 0; i <= cur; ++i) {
        if (strcmp(nodes[i]->name, name) == 0) {
            idx = i;
            break;
        }
    }
    if (offset > nodes[idx]->size) {
        return -1;
    }
    //之后开始读取文件，这里的实现思路与LAB5对应的read部分几乎相同。
    //先计算要读取的block no，读取对应block数据
    int to_read = 0;
    int cur_off = offset;
    int curRead = 0;
    int lba;
    int originSize = size;
    char readInfo[BLOCK_SIZE + 1];
    while (size > 0) {
        to_read = (size < BLOCK_SIZE) ? size : BLOCK_SIZE;
        //这里要注意，因为block 0已经用于保存元数据，所以计算出来的block id要依次+1.
        lba = cur_off / BLOCK_SIZE + 1;

        int lba_off = cur_off % BLOCK_SIZE;
        int left = BLOCK_SIZE - lba_off;
        sd_bread(lba, readInfo);
        // 要考虑一次需要的数据少于一个BLOCK_SIZE的情况。
        if (left < to_read) {
            for (int i = 0; i < left; ++i) {
                buffer[curRead + i] = readInfo[i + lba_off];
            }
        }
        else {
            for (int i = 0; i < to_read; ++i) {
                buffer[curRead + i] = readInfo[i + lba_off];
            }
        }
        buffer[curRead + to_read] = '\0';
        size -= to_read;
        curRead += to_read;
    }
    return originSize;
}

int naive_fs_pwrite(const char *name, int offset, int size, const char *buffer)
{
    // 首先检查文件的存在性，如果不存在则需要创建新的文件。
    if (naive_fs_access(name) != 0) {
        naive_fs_creat(name);
    }
    int idx = -1;
    for (int i = 0; i <= cur; ++i) {
        if (strcmp(nodes[i]->name, name) == 0) {
            idx = i;
            break;
        }
    }
    //之后开始读取文件，思路与LAB5类似。
    int originSize = size;
    int originLen = strlen(buffer);
    int to_write;
    int cur_off = offset;
    int lba;
    int lba_off;
    int curWrite = 0;
    while (size > 0) {
        //注意block 0已经用于存储元数据信息，所以要+1来找到存储文件数的block no。
        lba = cur_off / BLOCK_SIZE + 1;
        char* tmp = (char*)malloc(BLOCK_SIZE + 1);
        if (size >= BLOCK_SIZE) {
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                tmp[i] = buffer[i];
            }
        }
        // 要额外考虑要进行写入的buffer剩余数据量已经少于一个BLOCK_SIZE的情况。
        else {
            for (int i = 0; i < size; ++i) {
                tmp[i] = buffer[i];
            }
            for (int i = size; i < BLOCK_SIZE; ++i) {
                tmp[i] = 0;
            }
        }
        tmp[BLOCK_SIZE] = '\0';
        int ret = sd_bwrite(lba, tmp);
        size -= BLOCK_SIZE;
        buffer += BLOCK_SIZE;
        cur_off += BLOCK_SIZE;
    }
    if (cur_off > nodes[idx]->size) {
        nodes[idx]->size = cur_off;
    }
    // 修改文件元数据信息，因为文件大小等数据可能已经被修改。
    MarshalMetadata();
    return originSize;
}

int naive_fs_unlink(const char *name)
{
    //首先要确保文件已经存在在系统中。
    if (naive_fs_access(name) != 0) {
        return -1;
    }
    for (int i = 0; i <= cur; ++i) {
        if (strcmp(nodes[i]->name, name) == 0) {
            nodes[i] = NULL;
			// 由于已经删除了一个文件，所以要再次序列化。
            MarshalMetadata();
            return 0;
        }
    }
    return -2;
}
```



**思考2.1**：查阅资料了解 SD 卡是如何进行分区，又是如何识别分区对应的文件系统的？

**回答**：

- **如何进行分区**：

  - 首先进行`SD`卡的坏块检查并对坏块进行处理

  - 使用分区工具(如`fdisk`)进行分区的划分。

  - 之后对于每一个分区使用一个文件系统(如`NTFS,FAT32,EXT4,EXT2`等)对于每一个分区进行格式化。

  - 格式化之后文件系统就可以正常使用了。

- **如何识别分区对应的文件系统**：

  - 首先读取主引导分区中的元数据
  - 通过主引导分区中的元数据，通过其中不同文件系统的独特的标记位(魔数)进行文件系统种类的识别。
  - 之后根据识别得到的文件系统信息加载对应的文件系统驱动来解析文件系统的元数据进行文件系统的初始化或数据恢复。

  

**思考2.2**：尝试设计方案为`ChCore`提供多分区的`SD`卡驱动支持，设计其解析与挂载流程。

**回答**：

首先设计`MBR`表结构如下。在这里只考虑在`LAB5`中使用到的两种文件系统`tmpfs`以及`fakefs`。并且在这里涉及的数据结构中，考虑一种4个主分区加上4个拓展分区的分区形式。

```c
//使用tmpfs时候的根inode以及根目录
struct tmpRoot {
    struct inode* root;
    struct dentry* root_dent;
};
//使用fakefs时候的根节点。
struct fakeRoot {
    struct fake_fs_file_node* root;
};
//以union的形式封装两种节点.
struct root_node {
    union {
        struct tmpRoot* tmp_root;
        struct fakeRoot* fake_root;
    };
};
//分区具体的数据结构。
struct SDPartition {
    // 0 <--> 主分区
    // 1 <--> 拓展分区。
    int partitionType;
    // 0 <--> tmpfs
    // 1 <--> fakefs
    int FSType;
    // 使用的文件系统的根目录位置
    char* mountPath;
    // 文件系统可以使用的block数量
    int blockNum;
    // 使用的block ids(未占用的index使用-1占位)
    int* blocks;
    // 根节点信息
    struct root_node* root;
};
//MBR主引导分区数据结构。
struct MBR {
    // 4 main partitions + 4 extended partitions
    struct SDPartition partitions[8];
};
```

之后，需要在进行`SD`卡初始化的时候，先创建对应的分区信息，并将其序列化之后存入`SD`卡的第一个分区。(即将第一个分区设置为`super block`)。代码如下：

```c
void SDMBRInit()
{
    struct MBR* mbr = (struct MBR*) malloc(sizeof(struct MBR));
    for (int i = 0; i < 8; ++i) {
        mbr->partitions[i].partitionType = (i < 4) ? 0 : 1;
        mbr->partitions[i].FSType = (i < 4) ? 0 : 1;
        mbr->partitions[i].blockNum = 32;
        mbr->partitions[i].blocks = malloc(4 * mbr->partitions[i].blockNum);
        mbr->partitions[i].mountPath = paths[i];
        mbr->partitions[i].root = (struct root_node*) malloc(sizeof(struct root_node));
        //根据初始化的文件系统种类在不同目录上面挂载不同的文件系统。
        if (mbr->partitions[i].FSType == 0) {
            init_sd_tmpfs(mbr->partitions[i].mountPath, mbr->partitions[i].root->tmp_root->root, mbr->partitions[i].root->tmp_root->root_dent);
        } else {
            sd_fakefs_init(mbr->partitions[i].mountPath, mbr->partitions[i].root->fake_root->root);
        }
    }
    // 将数据写入super block中去。
    char* data;
    MarshalMBR(data, mbr);
}
```

在`SD`卡初始化完成时候，之后系统初次启动的时候，都需要先读取`super block`中的元数据进行文件系统的初始化挂载。代码如下。

```c
void SDMBRReinit()
{
    struct MBR* mbr;
    char* data;
    UnmarshalMBR(data, mbr);
    for (int i = 0; i < 8; ++i) {
        if (mbr->partitions[i].FSType == 0) {
            //根据源数据中指定的文件系统类型进行不同文件系统的初始化。
            init_sd_tmpfs(mbr->partitions[i].mountPath, mbr->partitions[i].root->tmp_root->root, mbr->partitions[i].root->tmp_root->root_dent);
        } else {
            sd_fakefs_init(mbr->partitions[i].mountPath, mbr->partitions[i].root->fake_root->root);
        }
    }
}
```

在上述代码中，`init_fs`相关的逻辑基本服用了`LAB5`提供的框架，在此基础上添加了自己需要的参数。新的初始化函数如下。函数中额外涉及的`MarshalMBR`以及`UnmarshalMBR`函数就是将数据结构序列化存入超级块以及从超级块中取出数据之后进行反序列化操作，再次不再列出。

```c
//tmpfs.
int init_sd_tmpfs(char* rootPath, struct inode* root, struct dentry* root_dent)
{
	root = new_dir();
	root_dent = new_dent(root, rootPath, 1);
	init_id_manager(&fidman, MAX_NR_FID_RECORDS, DEFAULT_INIT_ID);
	chcore_assert(alloc_id(&fidman) == 0);
	init_fs_wrapper();
	mounted = true;
	return 0;
}
// fakefs.
void sd_fakefs_init(char* rootPath, struct fakefs_file_node* root) 
{
	init_list_head(&fakefs_files);
	spinlock_init(&fs_lock);
	struct fakefs_file_node *n = (struct fakefs_file_node *)malloc(sizeof(struct fakefs_file_node));
	init_fakefs_file_node(root);
	strcpy(root->path, rootPath);
	n->isdir = true;
	/* Insert node to fsm_server_entry_mapping */
	list_append(&root->node, &fakefs_files);
}
```

**注：本部分只是关于思考题的实现思路，虽然写了部分代码，但是并没有通过测试程序的方式验证正确性。**



**可能的改进方向**：

- 可以对于`LAB6`中的`naive_fs`部分进行改进，实现支持`SD`卡分区驱动的块设备文件系统。
- 可以对于`LAB5`中的`SHELL`部分进行改进，支持输入内容的回退。
- 对于`CHCORE`的建议，希望之后每次`LAB`结束之后，可以公布这个`LAB`的思考题以及简答题答案。

