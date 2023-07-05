/*
 * Copyright (c) 2022 Institute of Parallel And Distributed Systems (IPADS)
 * ChCore-Lab is licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 */

#include <stdio.h>
#include <string.h>
#include <chcore/types.h>
#include <chcore/fsm.h>
#include <chcore/tmpfs.h>
#include <chcore/ipc.h>
#include <chcore/internal/raw_syscall.h>
#include <chcore/internal/server_caps.h>
#include <chcore/procm.h>
#include <chcore/fs/defs.h>
// include a new header file here.
// #include <libc/FILE.h>

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)
#define va_copy(d, s)  __builtin_va_copy(d, s)


extern struct ipc_struct *fs_ipc_struct;
const int READ_MODE = 0;
const int WRITE_MODE = 1;
const int READ_WRITE_MODE = 2;

// functions in this file should use IPC to send out user requirement.
// can take a look at the implementation of function read_file_from_tfs() in tmpfs.c.

/* You could add new functions or include headers here.*/
/* LAB 5 TODO BEGIN */
void connect_tmpfs_server_self(void)
{
        int tmpfs_cap = __chcore_get_tmpfs_cap();
        chcore_assert(tmpfs_cap >= 0);
        fs_ipc_struct = ipc_register_client(tmpfs_cap);
        chcore_assert(fs_ipc_struct);
}

int getFileMode(const char* mode) {
	if (!mode || *mode == '\0') {
		return -1;
	}
	if (strlen(mode) == 1 && *mode == 'w') {
		return WRITE_MODE;
	}
	if (strlen(mode) == 1 && *mode == 'r') {
		return READ_MODE;
	}
	return READ_WRITE_MODE;
}

int convertStrToInt(char* buf, int* res) {
	while (*buf == ' ') {
		buf++;
	}
	int num = 0;
	while (*buf >= '0' && *buf <= '9') {
		num = 10 * num + (*buf) - '0';
		buf++;
	}
	*res = num;
	return num;
}

void convertIntToStr(char* buf, int val) {
	char tmp[8192];
	int i = 0;
	while (val != 0) {
		int num = val % 10;
		val /= 10;
		tmp[i] = num + '0';
		i++;
	}
	tmp[i] = '\0';
	for (int j = 0; j < i; ++j) {
		buf[j] = tmp[i - j - 1];
	}
	buf[i] = '\0';
}

/* LAB 5 TODO END */


FILE *fopen(const char * filename, const char * mode) {

	/* LAB 5 TODO BEGIN */
	if (fs_ipc_struct == NULL) {
		// connect to fs server here.
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
	file_fd = ipc_call(fs_ipc_struct, ipc_msg);
	ipc_destroy_msg(fs_ipc_struct, ipc_msg);
    if (file_fd < 0) {
		// the target file does not exist.
		// so send another IPC to create a new file according to the information.
		struct ipc_msg* ipc_msg = ipc_create_msg(fs_ipc_struct, sizeof(struct fs_request), 0);
		chcore_assert(ipc_msg);
		struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(ipc_msg);
		fr->req = FS_REQ_CREAT;// create a new file.
		fr->creat.mode = getFileMode(mode);// set new created file mode.
		strcpy(fr->creat.pathname, filename);
		ipc_call(fs_ipc_struct, ipc_msg);
		ipc_destroy_msg(fs_ipc_struct, ipc_msg);
		

		// then send a new IPC to re-open the new file to get its fd.
		struct ipc_msg* ipc_msg_1 = ipc_create_msg(fs_ipc_struct, sizeof(struct fs_request), 0);
		chcore_assert(ipc_msg_1);
		struct fs_request* fr1 = (struct fs_request*)ipc_get_msg_data(ipc_msg_1);
		fr1->req = FS_REQ_OPEN;
		strcpy(fr1->open.pathname, filename);
		fr1->open.flags = O_RDONLY;
		fr1->open.new_fd = 1;
		file_fd = ipc_call(fs_ipc_struct, ipc_msg_1);
		ipc_destroy_msg(fs_ipc_struct, ipc_msg_1);
	}
	// then create a new FILE and return.
	FILE* f = malloc(sizeof(FILE));
	f->fd = file_fd;
	f->off = 0;
	/* LAB 5 TODO END */
    return f;
}

// src: data that will be written into FILE f.
// f: target file.
// size: data type size? (see shell_test.c)
// nmemb: write data size.
size_t fwrite(const void * src, size_t size, size_t nmemb, FILE * f) {

	/* LAB 5 TODO BEGIN */
	int ret;
	// first try to locate the start pos that wants to write.
	size_t totalSize = sizeof(struct fs_request) + nmemb * size + 1;
	struct ipc_msg* ipc_msg = ipc_create_msg(fs_ipc_struct, sizeof(struct fs_request), 0);
	chcore_assert(ipc_msg);
	struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(ipc_msg);
	fr->req = FS_REQ_LSEEK;
	fr->lseek.fd = f->fd;
	fr->lseek.offset = f->off;
	fr->lseek.whence = 0; // ???
	ret = ipc_call(fs_ipc_struct, ipc_msg);
	ipc_destroy_msg(fs_ipc_struct, ipc_msg);
	// then write the data into the file from the start pos.
	// a pronlem here: how to send the write data through IPC ???
	struct ipc_msg* write_ipc_msg = ipc_create_msg(fs_ipc_struct, totalSize, 0);
	chcore_assert(ipc_msg);
	struct fs_request* write_fr = (struct fs_request*)ipc_get_msg_data(write_ipc_msg);
	write_fr->req = FS_REQ_WRITE;
	write_fr->write.fd = f->fd;
	// note the '\0' !
	write_fr->write.count = nmemb * size;
	// then put data into ipc_msg.
	
	memcpy((void *) write_fr + sizeof(struct fs_request), src, nmemb * size);
	ret = ipc_call(fs_ipc_struct, write_ipc_msg);
	if (ret < 0) {
		// write error occurs.
		ipc_destroy_msg(fs_ipc_struct, write_ipc_msg);
		return ret;
	}
	ipc_destroy_msg(fs_ipc_struct, write_ipc_msg);
	// move the file cusor.
	// NOTE: do not forget to multi the size of data type !
	f->off += nmemb * size;
	// then close the target file.

	/* LAB 5 TODO END */
    return ret;

}

// args: similar to func fwrite, the difference is that destv is the var to store the final data read from the target file.
size_t fread(void * destv, size_t size, size_t nmemb, FILE * f) {

	/* LAB 5 TODO BEGIN */
	int ret;
	// first try to locate the start pos that wants to read.
	// uint64_t totalSize = sizeof(struct fs_request) + nmemb + 1;
	struct ipc_msg* ipc_msg = ipc_create_msg(fs_ipc_struct, sizeof(struct fs_request), 0);
	chcore_assert(ipc_msg);
	struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(ipc_msg);
	fr->req = FS_REQ_LSEEK;
	fr->lseek.fd = f->fd;
	fr->lseek.offset = f->off;
	fr->lseek.whence = 0; // ???
	ret = ipc_call(fs_ipc_struct, ipc_msg);
	ipc_destroy_msg(fs_ipc_struct, ipc_msg);
	// then send a new IPC to read data from the target file.
	size_t totalSize = sizeof(struct fs_request);
	struct ipc_msg* read_ipc_msg = ipc_create_msg(fs_ipc_struct, totalSize, 0);
	chcore_assert(read_ipc_msg);
	struct fs_request* fr1 = (struct fs_request*)ipc_get_msg_data(read_ipc_msg);
	fr1->req = FS_REQ_READ;
	fr1->read.fd = f->fd;
	fr1->read.count = nmemb * size;
	ret = ipc_call(fs_ipc_struct, read_ipc_msg);
	if (ret > 0) {
		char* gotData = ipc_get_msg_data(read_ipc_msg);
		memcpy(destv, gotData, ret);
		f->off += nmemb * size; // remember to update the file cursor position!
	}
	ipc_destroy_msg(fs_ipc_struct, read_ipc_msg);
	/* LAB 5 TODO END */
    return ret;

}

int fclose(FILE *f) {

	/* LAB 5 TODO BEGIN */
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

/* Need to support %s and %d. */
int fscanf(FILE * f, const char * fmt, ...) {

	/* LAB 5 TODO BEGIN */
	va_list args;
	va_start(args, fmt);
	char data[8192];
	int readSize = 256; // for the test only ??
	fread(data, sizeof(char), readSize, f);
	char* tmp = (char*) data;
	bool startFlag = false;
	char arg1[8192];
	memcpy(arg1, fmt, strlen(fmt));
	char* arg = (char*)arg1;
	arg[strlen(fmt)] = '\0';
	while (*arg != '\0') {
		if (startFlag == false) {
			if (*arg == '%') {
				startFlag = true;
			}
			else {
				if (*arg != *tmp) {
					// for the part that is not flags, should read sync.
					return -1;
				}
				else {
					tmp++;
				}
			}
		}
		else {
			startFlag = false;
			if (*arg == 'd') {
				// read a int from the stream.
				int* int_val = va_arg(args, int*);
				// convert a part of the string into integer.
				convertStrToInt(tmp, int_val);
				while (*tmp != ' ') {
					tmp++;
				}
			}
			if (*arg == 's') {
				char* str_val = va_arg(args, char*);
				char tmpVal[8192];
				int i = 0;
				while (*tmp != '\0' && *tmp != ' ') {
					tmpVal[i] = *tmp;
					tmp++;
					i++;
				}
				tmpVal[i] = '\0';
				memcpy(str_val, tmpVal, strlen(tmpVal) + 1);
				while (*tmp != ' ') {
					tmp++;
				}
			}
		}
		arg++;
	}
	// va_end(args);
	/* LAB 5 TODO END */
    return 0;
}

/* Need to support %s and %d. */
int fprintf(FILE * f, const char * fmt, ...) {
	/* LAB 5 TODO BEGIN */
	va_list args;
	va_start(args, fmt);
	char data[8192];
	char arg1[8192];
	memcpy(arg1, fmt, strlen(fmt));
	char* arg = (char*)arg1;
	arg[strlen(fmt)] = '\0';
	bool startFlag = false;
	data[0] = '\0';
	int len = 0;
	while (*arg != '\0') {
		char c = *arg;
		if (startFlag == false) {
			if (c == '%') {
				startFlag = true;
			}
			else {
				data[len] = c;
				data[len + 1] = '\0';
				len += 1;
			}
		}
		else {
			startFlag = false;
			if (c == 'd') {
				int int_val = va_arg(args, int);
				char tmp_buf[8192];
				convertIntToStr(tmp_buf, int_val);
				for (int j = 0; j < strlen(tmp_buf); ++j) {
					data[len + j] = tmp_buf[j];
				}
				len += strlen(tmp_buf);
				data[len] = '\0';
			}
			if (c == 's') {
				char* char_val = (char*)(va_arg(args, char*));
				size_t char_val_len = strlen(char_val);
				for (int j = 0; j < char_val_len; ++j) {
					data[len + j] = *char_val;
					char_val++;
				}
				len += char_val_len;
				data[len] = '\0';
			}
		}
		arg++;
	}
	va_end(args);
	int res = fwrite(data, sizeof(char), strlen(data), f);
	/* LAB 5 TODO END */
    return res;
}

