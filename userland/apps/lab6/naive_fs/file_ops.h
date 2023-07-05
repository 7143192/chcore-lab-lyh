#pragma once

struct naiveNode {
    char* name;
    int id;
    int blockNum;
    int* blocks;
    int size;
    int offset;
};
struct naiveNode* nodes[1024];
// std::map<int, *naiveNode> nodeMap;
struct tmpRoot {
    struct inode* root;
    struct dentry* root_dent;
};

struct fakeRoot {
    struct fake_fs_file_node* root;
};

struct root_node {
    union {
        struct tmpRoot* tmp_root;
        struct fakeRoot* fake_root;
    };
};

struct SDPartition {
    // 0 <--> main partition
    // 1 <--> extended partition
    int partitionType;
    // filesystem type to mount in this partition. 
    // 0 <--> tmpfs
    // 1 <--> fakefs
    // 2 <--> ext4....
    // maybe only consider tmpfs and fakefs here?
    int FSType;
    // FS mount path.
    char* mountPath;
    // block number allocated to this partition.
    int blockNum;
    // block ids used for this partition.
    int* blocks;
    // root info. 
    struct root_node* root;
};

struct MBR {
    // 4 main partitions + 4 extended partitions
    struct SDPartition partitions[8];
};
// void UnmarshalMBR(char* data, struct MBR* mbr);
// void MarshalMBR(char* data, struct MBR* mbr);
// void SDMBRInit();
void MarshalMetadata();
void UnmarshalMetadata();
int naive_fs_access(const char *name);
int naive_fs_creat(const char *name);
int naive_fs_pread(const char *name, int offset, int size, char *buffer);
int naive_fs_pwrite(const char *name, int offset, int size, const char *buffer);
int naive_fs_unlink(const char *name);