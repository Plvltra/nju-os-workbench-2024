#include <stdio.h>
#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <sched.h>
#include <string.h>
#include <stdbool.h>

#define COMM_LEN 64

static char* PROC_BASE = "/proc";

static int by_pid = 0, pids = 0;

void print_version()
{
	fprintf(stderr, "pstree (PSmisc) UNKNOWN\n");
}

typedef struct _proc
{
	pid_t pid;
	char msg[COMM_LEN + 1];

	struct _proc *bro;
	struct _proc *child;

	struct _proc *next;
} Proc;

static Proc* list = NULL;

Proc* find_proc(pid_t pid){
	Proc* p = list;
	while (p)
	{
		if (p->pid == pid)
			return p;
		p = p->next;
	}
	return NULL;
}

Proc* new_proc(pid_t pid, char* msg){
	Proc* new = malloc(sizeof(Proc));
	new->pid = pid;
	strcpy(new->msg, msg);

	new->next = list;
	list = new;
	return new;
}

bool get_proc_info(pid_t pid, char* out_msg, pid_t* out_ppid)
{
	char readbuf[BUFSIZ + 1];
	char* path = malloc(strlen(PROC_BASE) + 20 + 10);
	sprintf(path, "%s/%d/stat", PROC_BASE, pid);
	FILE* file;

	if ((file = fopen(path, "r")) != NULL) {
		size_t bytes_read = fread(readbuf, 1, BUFSIZ, file);
		char* comm_start = strchr(readbuf, '(');
		char* comm_end = strrchr(readbuf, ')');
		*comm_end = 0;
		strcpy(out_msg, comm_start + 1);
		pid_t ppid;
		if (sscanf(comm_end + 3, "%*c %d", &ppid) == 1) {
			*out_ppid = ppid;
		}

		fclose(file);
		free(path);
		return true;
	}
	free(path);
	return false;
}

int num_pid(pid_t pid) {
	int count = 0;
	while (pid != 0) {
		pid = pid / 10;
		count++;
	}
	return count;
}

void debug_tree() {
	printf("pid\tmsg\t\t\tcpid\n");
	Proc* list_head = list;
	while (list_head) {
		printf("%d\t%s\t\t\t", list_head->pid, list_head->msg);
		Proc* child = list_head->child;
		while (child) {
			printf("%d ", child->pid);
			child = child->bro;
		}
		printf("\n");
		list_head = list_head->next;
	}
	printf("\n");
}

void dump_tree(Proc* proc, int* buffer, int size) {
	if (proc == NULL)
		return;

	while (proc)
	{
		printf("%s", proc->msg);
		if (pids)
			printf("(%d)", proc->pid);

		// print_child
		Proc* child = proc->child;
		if (child == NULL) {
			printf("\n");
		}

		int child_size = size + strlen(proc->msg) + pids * (num_pid(proc->pid) + 2) + 3; // indent that should be printed from the second bro
		if (child) {
			if (child->bro != NULL) {
				printf("\u2500\u252c\u2500"); // ─┬─
				buffer[child_size - 2] = 1;
			} else {
				printf("\u2500\u2500\u2500"); // ───
			}
			dump_tree(child, buffer, child_size);
		}

		proc = proc->bro;
		if (proc) {
			if (!proc->bro)
				buffer[size - 2] = 0;
			
			for (int i = 0; i < size - 2; i++) {
				if (buffer[i]) {
					printf("\u2502"); // │
				} else {
					printf(" ");
				}
			}
			if (proc->bro) {
				printf("\u251c\u2500"); // ├─
			} else {
				printf("\u2514\u2500"); // └─
			}
		}
	}
}

int main(int argc, char *argv[])
{
	int c;
	struct option options[] = {
		{"numeric-sort", no_argument, NULL, 'n'},
		{"show-pids", no_argument, NULL, 'p'},
		{"version", no_argument, NULL, 'V'},
		{0, 0, 0, 0}};
	while ((c = getopt_long(argc, argv, "npV",
							options, NULL)) != -1)
	{
		switch (c)
		{
		case 'n':
			by_pid = 1;
			break;
		case 'p':
			pids = 1;
			break;
		case 'V':
			print_version();
			return 0;
		default:
			fprintf(stderr, "Invalid argument\n");
			exit(EXIT_FAILURE);
		}
	}

	if (optind != argc)
	{
		fprintf(stderr, "Expected argument after options\n");
		exit(EXIT_FAILURE);
	}

	DIR *dir;
	struct dirent *ent;
	char readbuf[BUFSIZ + 1];

	if ((dir = opendir(PROC_BASE)) != NULL)
	{
		while ((ent = readdir(dir)) != NULL)
		{
			pid_t pid;
			if ((pid = atoi(ent->d_name)) == 0)
				continue;

			char msg[COMM_LEN + 1];
			pid_t ppid;
			get_proc_info(pid, msg, &ppid);
			Proc* proc = find_proc(pid);
			if (proc == NULL) {
				proc = new_proc(pid, msg);
			}
			Proc* parent_proc = find_proc(ppid);
			if (parent_proc == NULL) {
				pid_t pppid;
				get_proc_info(ppid, msg, &pppid);
				parent_proc = new_proc(ppid, msg);
			}

			if (by_pid) {
				Proc* head = parent_proc->child;
				if (head == NULL || head->pid > proc->pid) {
					proc->bro = parent_proc->child;
					parent_proc->child = proc;
				} else {
					while (head && head->pid < proc->pid
						&& head->bro && head->bro->pid < proc->pid) {
						head = head->bro;
					}
					proc->bro = head->bro;
					head->bro = proc;
				}
			} else {
				proc->bro = parent_proc->child;
				parent_proc->child = proc;
			}
		}
		closedir(dir);

		debug_tree();

		Proc* root_proc = find_proc(1);
		int buffer[1024] = {0};
		dump_tree(root_proc, buffer, 0);
	}
	else
	{
		return EXIT_FAILURE;
	}

	return 0;
}
