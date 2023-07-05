#include <string.h>
#include <stdio.h>
#include "file_ops.h"
#include "block_layer.h"
int cur = 0;
char* paths[8] = {"/", "/a/", "/b/", "/c/", "/d/", "/e/", "/f/", "/g/"};
void MarshalMetadata()
{
    char* buf = (char*) malloc(BLOCK_SIZE + 1);
    for (int i = 0; i <= BLOCK_SIZE; ++i) {
        buf[i] = 0;
    }
    int pos = 0;
    char* curStr = (char*) &cur;
    // strcat(buf, curStr);
    for (int i = 0; i < 4; ++i) {
        buf[pos++] = curStr[i];
    }
    // printf("finish marshal cur!\n");
    for (int i = 0; i < cur; ++i) {
        int len = strlen(nodes[i]->name);
        char* lenStr = (char*) &len;
        // strcat(buf, lenStr);
        for (int j = 0; j < 4; ++j) {
            buf[pos++] = lenStr[j];
        }
        // strcat(buf, nodes[i]->name);
        for (int j = 0; j < len; ++j) {
            buf[pos++] = nodes[i]->name[j];
        }
        char* idStr = (char*) &(nodes[i]->id);
        // strcat(buf, idStr);
        for (int j = 0; j < 4; ++j) {
            buf[pos++] = idStr[j];
        }
        char* sizeStr = (char*) &(nodes[i]->size);
        // strcat(buf, sizeStr);
        for (int j = 0; j < 4; ++j) {
            buf[pos++] = sizeStr[j];
        }
        char* offStr = (char*) &(nodes[i]->offset);
        // strcat(buf, offStr);
        for (int j = 0; j < 4; ++j) {
            buf[pos++] = offStr[j];
        }
    }
    buf[pos] = '\0';
    // printf("buf = %s\n", buf);
    sd_bwrite(0, buf);
    // printf("buf = %s\n", buf);
    // free(buf);
}

void UnmarshalMetadata()
{
    char* buf = (char*) malloc(BLOCK_SIZE + 1);
    for (int i = 0; i <= BLOCK_SIZE; ++i) {
        buf[i] = 0;
    }
    sd_bread(0, buf);
    // printf("buf = %s\n", buf);
    int curNum = *(int*)buf;
    buf += sizeof(int);
    cur = curNum;
    // printf("buf = %s\n", buf);
    for (int i = 0; i < cur; ++i) {
        nodes[i] = (struct naiveNode*) malloc(sizeof(struct naiveNode));
        int len = *(int*) buf;
        buf += 4;
        nodes[i]->name = (char*) malloc(len + 1);
        strncpy(nodes[i]->name, buf, len);
        nodes[i]->name[len] = '\0';
        // printf("name len = %s\n", nodes[i]->name);
        buf += len;
        int id = *(int*) buf;
        buf += 4;
        nodes[i]->id = id;
        // printf("node id = %d\n", id);
        int size = *(int*) buf;
        buf += 4;
        nodes[i]->size = size;
        int off = *(int*) buf;
        buf += 4;
        nodes[i]->offset = off;
    }
    free(buf);
    // printf("finish Unmarshal function!\n");
}

int naive_fs_access(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */

    // add persist logic here.
    UnmarshalMetadata();
    // printf("finish unmarshal function!\n");

    for (int i = 0; i <= cur; ++i) {
        if (nodes[i] == NULL) {
            continue;
        }
        if (strcmp(nodes[i]->name, name) == 0) {
            return 0;
        }
    }
    /* BLANK END */
    /* LAB 6 TODO END */
    return -1;
}

int naive_fs_creat(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if (naive_fs_access(name) == 0) {
        return -1;
    }
    struct naiveNode* node = (struct naiveNode*) malloc(sizeof(struct naiveNode));
    node->offset = 0;
    node->size = 0;
    // printf("get here!\n");
    node->name = (char*) malloc(strlen(name) + 1);
    strcpy(node->name, name);
    // printf("new node->name = %s\n", node->name);
    nodes[cur] = node;
    node->id = cur + 1;
    node->blockNum = 0;
    node->blocks = (int*) malloc(sizeof(int) * 10);
    cur++;

    // add persist logic.
    MarshalMetadata();

    return 0;
    /* BLANK END */
    /* LAB 6 TODO END */
    // return -2;
}

int naive_fs_pread(const char *name, int offset, int size, char *buffer)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
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
    int to_read = 0;
    int cur_off = offset;
    int curRead = 0;
    int lba;
    int originSize = size;
    char readInfo[BLOCK_SIZE + 1];
    while (size > 0) {
        to_read = (size < BLOCK_SIZE) ? size : BLOCK_SIZE;
        // lba = cur_off / BLOCK_SIZE;

        // add persist logic here.
        lba = cur_off / BLOCK_SIZE + nodes[idx]->id;

        int lba_off = cur_off % BLOCK_SIZE;
        int left = BLOCK_SIZE - lba_off;
        sd_bread(lba, readInfo);
        // char* tmp = (char*) malloc(to_read + 1);
        // for (int i = 0; i < to_read; ++i) {
        //     tmp[i] = readInfo[i + lba_off];
        // }
        // tmp[to_read] = '\0';
        // buffer[curRead] = '\0';
        // strcat(buffer, tmp);
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
    // buffer[originSize] = '\0';
    return originSize;
    /* BLANK END */
    /* LAB 6 TODO END */
    // return -2;
}

int naive_fs_pwrite(const char *name, int offset, int size, const char *buffer)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    // if the file does not exist, create a new one.
    if (naive_fs_access(name) != 0) {
        // printf("create a new file to write!\n");
        naive_fs_creat(name);
    }
    int idx = -1;
    for (int i = 0; i <= cur; ++i) {
        if (strcmp(nodes[i]->name, name) == 0) {
            idx = i;
            break;
        }
    }
    // printf("get write idx = %d\n", idx);
    int originSize = size;
    int originLen = strlen(buffer);
    int to_write;
    int cur_off = offset;
    int lba;
    int lba_off;
    int curWrite = 0;
    // what if the left size is less than BLOCK_SIZE?
    while (size > 0) {
        // lba = cur_off / BLOCK_SIZE;

        // add persist logic here.
        lba = cur_off / BLOCK_SIZE + nodes[idx]->id;

        char* tmp = (char*)malloc(BLOCK_SIZE + 1);
        if (size >= BLOCK_SIZE) {
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                tmp[i] = buffer[i];
            }
        }
        // consider the case that the left buffer is less than BLOCK_SIZE. 
        else {
            for (int i = 0; i < size; ++i) {
                tmp[i] = buffer[i];
            }
            for (int i = size; i < BLOCK_SIZE; ++i) {
                tmp[i] = 0;
            }
        }
        // for (int i = 0; i < BLOCK_SIZE; ++i) {
        //     tmp[i] = buffer[i];
        // }
        tmp[BLOCK_SIZE] = '\0';
        int ret = sd_bwrite(lba, tmp);
        // printf("sd write ret = %d\n", ret);
        size -= BLOCK_SIZE;
        buffer += BLOCK_SIZE;
        cur_off += BLOCK_SIZE;
        // originLen -= BLOCK_SIZE;
    }
    if (cur_off > nodes[idx]->size) {
        nodes[idx]->size = cur_off;
    }
    // printf("size = %d\n", size);

    // add persist logic here.
    MarshalMetadata();

    return originSize;
    /* BLANK END */
    /* LAB 6 TODO END */
    // return -2;
}

int naive_fs_unlink(const char *name)
{
    /* LAB 6 TODO BEGIN */
    /* BLANK BEGIN */
    if (naive_fs_access(name) != 0) {
        return -1;
    }
    for (int i = 0; i <= cur; ++i) {
        if (strcmp(nodes[i]->name, name) == 0) {
            nodes[i] = NULL;

            // add persist logic here.
            MarshalMetadata();

            return 0;
        }
    }
    /* BLANK END */
    /* LAB 6 TODO END */
    return -2;
}

// void MarshalMBR(char* data, struct MBR* mbr)
// {
//     data = (char*) malloc(sizeof(struct SDPartition) * 8 + 33);
//     *data = '\0';
//     // marshal every partition metadata here.
//     for (int i = 0; i < 8; ++i) {
//         int cur = 0;
//         char* tmp = (char*) malloc(sizeof(struct SDPartition) + 5);
//         char* c1 = (char*) &(mbr->partitions[i].partitionType);
//         char* c2 = (char*) &(mbr->partitions[i].FSType);
//         for (int j = 0; j < 4; ++j) {
//             tmp[cur++] = *c1;
//             c1++;
//         }
//         for (int j = 0; j < 4; ++j) {
//             tmp[cur++] = *c2;
//             c2++;
//         }
//         int len = strlen(mbr->partitions[i].mountPath);
//         char* lenC = (char*) &len;
//         for (int j = 0; j < 4; ++j) {
//             tmp[cur++] = *lenC;
//             lenC++;
//         }
//         char* tmpPath = mbr->partitions[i].mountPath;
//         for (int j = 0; j < len; ++j) {
//             tmp[cur++] = *tmpPath;
//             tmpPath++;
//         }
//         char* c3 = (char*) &(mbr->partitions[i].blockNum);
//         for (int j = 0; j < 4; j++) {
//             tmp[cur++] = *c3;
//             c3++;
//         }
//         for (int k = 0; k < mbr->partitions[i].blockNum; ++k) {
//             char* c4 = (char*) &(mbr->partitions[i].blocks[k]);
//             for (int j = 0; j < 4; ++j) {
//                 tmp[cur++] = *c4;
//                 c4++;
//             }
//         }
//         char* c5 = (char*) mbr->partitions[i].root;
//         for (int j = 0; j < sizeof(struct SDPartition); ++j) {
//             tmp[cur++] = *c5;
//             c5++;
//         }
//         tmp[cur] = '\0';
//         strcat(data, tmp);
//     }
//     // then write the marshaled data into sd card's block 0.(as the super block).
//     const char* finalData = data;
//     sd_bwrite(0, finalData);
// }

// void UnmarshalMBR(char* data, struct MBR* mbr)
// {
//     // read marshal data from SD card first.
//     sd_bread(0, data);
//     mbr = (struct MBR*)malloc(sizeof(struct MBR));
//     for (int i = 0; i < 8; ++i) {
//         int type1 = *(int*)data;
//         data += 4;
//         mbr->partitions[i].partitionType = type1;
//         int type2 = *(int*) data;
//         data += 4;
//         mbr->partitions[i].FSType = type2;
//         int len = *(int*) data;
//         data += 4;
//         strncpy(mbr->partitions[i].mountPath, data, len);
//         data += len;
//         int num = *(int*) data;
//         data += 4;
//         mbr->partitions[i].blockNum = num;
//         mbr->partitions[i].blocks = (int*) malloc(sizeof(int) * num);
//         for (int j = 0; j < num; ++j) {
//             int id = *(int*) data;
//             data += 4;
//             mbr->partitions[i].blocks[j] = id;
//         }
//         struct root_node* root = (struct root_node*) data;
//         data += sizeof(struct root_node);
//         mbr->partitions[i].root = root;
//     }
// }

// void SDMBRInit()
// {
//     struct MBR* mbr = (struct MBR*) malloc(sizeof(struct MBR));
//     for (int i = 0; i < 8; ++i) {
//         mbr->partitions[i].partitionType = (i < 4) ? 0 : 1;
//         mbr->partitions[i].FSType = (i < 4) ? 0 : 1;
//         mbr->partitions[i].blockNum = 32;
//         mbr->partitions[i].blocks = malloc(4 * mbr->partitions[i].blockNum);
//         mbr->partitions[i].mountPath = paths[i];
//         mbr->partitions[i].root = (struct root_node*) malloc(sizeof(struct root_node));
//         // 0 <--> init as a tmpfs.
//         if (mbr->partitions[i].FSType == 0) {
//             init_sd_tmpfs(mbr->partitions[i].mountPath, mbr->partitions[i].root->tmp_root->root, mbr->partitions[i].root->tmp_root->root_dent);
//         } else {
//             sd_fakefs_init(mbr->partitions[i].mountPath, mbr->partitions[i].root->fake_root->root);
//         }
//     }
//     // write data to super block.
//     char* data;
//     MarshalMBR(data, mbr);
// }

// void SDMBRReinit()
// {
//     struct MBR* mbr;
//     char* data;
//     UnmarshalMBR(data, mbr);
//     for (int i = 0; i < 8; ++i) {
//         // reinit every partition's fs.
//         if (mbr->partitions[i].FSType == 0) {
//             init_sd_tmpfs(mbr->partitions[i].mountPath, mbr->partitions[i].root->tmp_root->root, mbr->partitions[i].root->tmp_root->root_dent);
//         } else {
//             sd_fakefs_init(mbr->partitions[i].mountPath, mbr->partitions[i].root->fake_root->root);
//         }
//     }
// }
