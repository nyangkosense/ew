#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* macros */
#define MAX_LINES 1000
#define MAX_LINE_LENGTH 256
#define MAX_PATH 1024
#define MAX_COMMAND 128
#define MAX_FILES 1024

#define VCS_DIR ".svcs"
#define HISTORY_FILE ".svcs/history"
#define BACKUP_DIR ".svcs/versions"
#define INDEX_FILE ".svcs/index"

#define PRINT_SUCCESS(fmt, str) printf("%s" fmt "%s\n", GREEN, str, RESET)
#define PRINT_ERROR(msg, ...) printf("%s" msg "%s\n", RED, ##__VA_ARGS__, RESET)
#define CHECK_ARGS(n) if (argc < n) return ERR_NO_FILE
#define CHECK_FILE(f) if (access(f, F_OK) != 0) return ERR_NO_FILE
#define CHECK_REPO() if (access(VCS_DIR, F_OK) != 0) return ERR_NO_REPO
#define CHECK_HISTORY() if (access(HISTORY_FILE, F_OK) != 0) return ERR_NO_HISTORY
#define CHECK_TRACKED(f) if (!is_tracked(f)) return ERR_FILE_NOT_TRACKED

/* colors */
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define RESET "\x1b[0m"
#define CYAN "\x1b[36m"

typedef enum
{
	CMD_INIT,
	CMD_DIFF,
	CMD_FIND,
	CMD_SAVE,
	CMD_REVERT,
	CMD_HISTORY,
	CMD_STATUS,
	CMD_TRACK,
	CMD_UNTRACK,
	CMD_UNKNOWN
} Command;

typedef enum
{
	SUCCESS = 0,
	ERR_NO_REPO = -1,
	ERR_NO_HISTORY = -2,
	ERR_NO_FILE = -3,
	ERR_INVALID_VERSION = -4,
	ERR_FILE_NOT_TRACKED = -5,
	ERR_BINARY_FILE = -6,
	ERR_UNKNOWN_COMMAND = -7
} ErrorCode;

/* types */
typedef struct
{
	char lines[MAX_LINES][MAX_LINE_LENGTH];
	int line_count;
} FileContents;

typedef struct
{
	char filename[MAX_PATH];
	char username[MAX_PATH];
	time_t timestamp;
	int version;
	int lines_added;
	int lines_removed;
	char changed_lines[MAX_LINES][MAX_LINE_LENGTH];
	int num_changes;
	char change_types[MAX_LINES];
} EnhancedVersionInfo;

typedef struct
{
	char path[MAX_PATH];
	int is_tracked;
	time_t last_modified;
} TrackedFile;

/* function declarations */
static void compute_changes(const char *old_file, const char *new_file, EnhancedVersionInfo *info);
static void create_directory(const char *path);
static void diff(const char *filename);
static void diff_files(const char *file1, const char *file2);
static int file_exists(const char *filename);
static void history(void);
static void init(void);
static int is_tracked(const char *filepath);
static void track(const char *filepath);
static void untrack(const char *filepath);
static void status(void);
static void find_files(const char *path);
static void list_versions(const char *filename);
static void revert(const char *filename, int target_version);
static void save(const char *filename);
char *get_username(void);
static ErrorCode handle_command(Command cmd, int argc, char *argv[]);
FileContents read_file(const char *filename);


ErrorCode
handle_command(Command cmd, int argc, char *argv[])
{
	switch (cmd)
	{
	case CMD_INIT:
		init();
		return SUCCESS;

	case CMD_DIFF:
		CHECK_ARGS(3);
		CHECK_FILE(argv[2]);
		CHECK_REPO();
		diff(argv[2]);
		return SUCCESS;
	
	case CMD_FIND:
		CHECK_REPO();
		find_files(".");
		return SUCCESS;

	case CMD_SAVE:
		CHECK_ARGS(3);
		CHECK_FILE(argv[2]);
		CHECK_REPO();
		CHECK_TRACKED(argv[2]);
		save(argv[2]);
		return SUCCESS;

	case CMD_REVERT:
		CHECK_ARGS(4);
		CHECK_FILE(argv[2]);
		CHECK_REPO();
		CHECK_TRACKED(argv[2]);
		revert(argv[2], atoi(argv[3]));
		return SUCCESS;

	case CMD_HISTORY:
		CHECK_REPO();
		CHECK_HISTORY();
		history();
		return SUCCESS;

	case CMD_STATUS:
		CHECK_REPO();
		status();
		return SUCCESS;

	case CMD_TRACK:
		CHECK_ARGS(3);
		CHECK_FILE(argv[2]);
		CHECK_REPO();
		track(argv[2]);
		return SUCCESS;

	case CMD_UNTRACK:
		CHECK_ARGS(3);
		CHECK_FILE(argv[2]);
		CHECK_REPO();
		CHECK_TRACKED(argv[2]);
		untrack(argv[2]);
		return SUCCESS;

	default:
		return ERR_UNKNOWN_COMMAND;
	}
}

int 
is_tracked(const char *filepath)
{
	TrackedFile file;
	FILE *index = fopen(INDEX_FILE, "rb");
	if (!index)
		return 0;

	while (fread(&file, sizeof(TrackedFile), 1, index) == 1)
	{
		if (strcmp(file.path, filepath) == 0 && file.is_tracked)
		{
			fclose(index);
			return 1;
		}
	}
	fclose(index);
	return 0;
}

void 
track(const char *filepath)
{
	TrackedFile file;
	struct stat st;
	FILE *index = fopen(INDEX_FILE, "ab");

	if (stat(filepath, &st) != 0)
	{
		printf("%sFile does not exist: %s%s\n", RED, filepath, RESET);
		return;
	}

	if (S_ISDIR(st.st_mode))
	{
		printf("%sCannot track directory: %s%s\n", RED, filepath, RESET);
		return;
	}

	if (is_tracked(filepath))
	{
		printf("%sAlready tracking: %s%s\n", YELLOW, filepath, RESET);
		return;
	}

	strncpy(file.path, filepath, MAX_PATH - 1);
	file.is_tracked = 1;
	file.last_modified = st.st_mtime;

	if (!index)
	{
		printf("%sError opening index file%s\n", RED, RESET);
		return;
	}

	fwrite(&file, sizeof(TrackedFile), 1, index);
	fclose(index);

	printf("%sNow tracking: %s%s\n", GREEN, filepath, RESET);

	if (access(VCS_DIR, F_OK) == 0)
	{
		save(filepath);
	}
}

void 
untrack(const char *filepath)
{
	if (!is_tracked(filepath))
	{
		printf("%sFile is not tracked: %s%s\n", YELLOW, filepath, RESET);
		return;
	}

	char temp_index[MAX_PATH];
	snprintf(temp_index, sizeof(temp_index), "%s.tmp", INDEX_FILE);

	FILE *index = fopen(INDEX_FILE, "rb");
	FILE *temp = fopen(temp_index, "wb");

	if (!index || !temp)
	{
		printf("%sError updating index%s\n", RED, RESET);
		if (index)
			fclose(index);
		if (temp)
			fclose(temp);
		return;
	}

	TrackedFile file;
	while (fread(&file, sizeof(TrackedFile), 1, index) == 1)
	{
		if (strcmp(file.path, filepath) != 0)
		{
			fwrite(&file, sizeof(TrackedFile), 1, temp);
		}
	}

	fclose(index);
	fclose(temp);

	rename(temp_index, INDEX_FILE);
	printf("%sNo longer tracking: %s%s\n", GREEN, filepath, RESET);
}

void
status(void)
{
	TrackedFile file;
	struct stat st;
	FILE *index = fopen(INDEX_FILE, "rb");

	if (!index)
	{
		printf("%sNo tracked files%s\n", YELLOW, RESET);
		return;
	}

	printf("%sTracked files: %s\n", YELLOW, RESET);
	while (fread(&file, sizeof(TrackedFile), 1, index) == 1)
	{
		if (file.is_tracked)
		{
			if (stat(file.path, &st) == 0)
			{
				if (st.st_mtime > file.last_modified)
				{
					printf(" %s%s (modified)%s\n", RED, file.path, RESET);
				}
				else
				{
					printf(" %s%s%s\n", GREEN, file.path, RESET);
				}
			}
			else
			{
				printf(" %s%s (deleted)%s\n", RED, file.path, RESET);
			}
		}
	}
	fclose(index);
}

void 
find_files(const char *path)
{

	DIR *dir;
	struct dirent *entry;
	char full_path[MAX_PATH];

	dir = opendir(path);
	if (!dir)
	{
		printf("%sError opening directory: %s%s\n", RED, path, RESET);
		return;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		/* skip . , .. and .svcs dir */
		if (strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0 ||
			strcmp(entry->d_name, ".svcs") == 0)
		{
			continue;
		}

		if (strcmp(path, ".") == 0)
		{
			snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
		}
		else
		{
			snprintf(full_path, sizeof(full_path), "%s%s", path, entry->d_name);
		}

		struct stat path_stat;
		if (stat(full_path, &path_stat) == 0)
		{
			if (S_ISDIR(path_stat.st_mode))
			{
				find_files(full_path);
			}
			else if (S_ISREG(path_stat.st_mode))
			{
				if (is_tracked(full_path))
				{
					printf(" %s%s%s\n", GREEN, full_path, RESET);
				}
				else
				{
					printf(" %s%s (untracked)%s\n", YELLOW, full_path, RESET);
				}
			}
		}
	}
	closedir(dir);
}

void 
diff_files(const char *file1, const char *file2)
{
    FileContents old_content = read_file(file1);
    FileContents new_content = read_file(file2);
    
    const int M = old_content.line_count;
    const int N = new_content.line_count;

    int **L = malloc((M + 1) * sizeof(int *));
    for (int i = 0; i <= M; i++) {
        L[i] = calloc((N + 1), sizeof(int));
    }

    for (int i = 1; i <= M; i++) {
        for (int j = 1; j <= N; j++) {
            if (strcmp(old_content.lines[i-1], new_content.lines[j-1]) == 0) {
                L[i][j] = L[i-1][j-1] + 1;
            } else {
                L[i][j] = L[i-1][j] > L[i][j-1] ? L[i-1][j] : L[i][j-1];
            }
        }
    }
    
    printf("%s--- %s%s\n", RED, file1, RESET);
    printf("%s+++ %s%s\n", GREEN, file2, RESET);
    
    const int CONTEXT = 3;
    int i = M;
    int j = N;
    
    char change_type[MAX_LINES];
    char changed_lines[MAX_LINES][MAX_LINE_LENGTH];
    int change_count = 0;
    
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && strcmp(old_content.lines[i-1], new_content.lines[j-1]) == 0) {
            if (change_count < MAX_LINES) {
                change_type[change_count] = ' ';
                strcpy(changed_lines[change_count], old_content.lines[i-1]);
                change_count++;
            }
            i--; j--;
        } else if (j > 0 && (i == 0 || L[i][j-1] >= L[i-1][j])) {
            if (change_count < MAX_LINES) {
                change_type[change_count] = '+';
                strcpy(changed_lines[change_count], new_content.lines[j-1]);
                change_count++;
            }
            j--;
        } else if (i > 0) {
            if (change_count < MAX_LINES) {
                change_type[change_count] = '-';
                strcpy(changed_lines[change_count], old_content.lines[i-1]);
                change_count++;
            }
            i--;
        }
    }
    
    if (change_count > 0) {
        int old_lines = 0;
        int new_lines = 0;
        for (int k = 0; k < change_count; k++) {
            if (change_type[k] == '-' || change_type[k] == ' ') old_lines++;
            if (change_type[k] == '+' || change_type[k] == ' ') new_lines++;
        }
        
        printf("%s@@ -%d,%d +%d,%d @@%s\n", 
               CYAN,
               (M > CONTEXT ? M - CONTEXT : 1), old_lines,
               (N > CONTEXT ? N - CONTEXT : 1), new_lines,
               RESET);
        
        for (int k = change_count - 1; k >= 0; k--) {
            switch (change_type[k]) {
                case '+':
                    printf("%s+%s%s\n", GREEN, changed_lines[k], RESET);
                    break;
                case '-':
                    printf("%s-%s%s\n", RED, changed_lines[k], RESET);
                    break;
                case ' ':
                    printf(" %s\n", changed_lines[k]);
                    break;
            }
        }
        printf("\n");
    }
    
    for (int i = 0; i <= M; i++) {
        free(L[i]);
    }
    free(L);
}

int 
file_exists(const char *filename)
{
	return access(filename, F_OK) == 0;
}

FileContents 
read_file(const char *filename)
{
	FileContents content = {{{0}}, 0};
	FILE *file = fopen(filename, "r");
	if (!file)
		return content;

	while (content.line_count < MAX_LINES &&
		   fgets(content.lines[content.line_count], MAX_LINE_LENGTH, file))
	{
		char *newline = strchr(content.lines[content.line_count], '\n');
		if (newline)
			*newline = '\0';
		content.line_count++;
	}

	fclose(file);
	return content;
}

void 
compute_changes(const char *old_file, const char *new_file, EnhancedVersionInfo *info)
{
	FileContents old_content = read_file(old_file);
	FileContents new_content = read_file(new_file);

	info->lines_added = 0;
	info->lines_removed = 0;
	info->num_changes = 0;

	int i, j, lcs[MAX_LINES + 1][MAX_LINES + 1] = {{0}};

	for (i = 1; i <= old_content.line_count; i++)
	{
		for (j = 1; j <= new_content.line_count; j++)
		{
			if (strcmp(old_content.lines[i - 1], new_content.lines[j - 1]) == 0)
			{
				lcs[i][j] = lcs[i - 1][j - 1] + 1;
			}
			else
			{
				lcs[i][j] = (lcs[i - 1][j] > lcs[i][j - 1]) ? lcs[i - 1][j] : lcs[i][j - 1];
			}
		}
	}

	i = old_content.line_count;
	j = new_content.line_count;

	while (i > 0 || j > 0)
	{
		if (i > 0 && j > 0 && strcmp(old_content.lines[i - 1], new_content.lines[j - 1]) == 0)
		{
			i--;
			j--;
		}
		else if (j > 0 && (i == 0 || lcs[i][j - 1] >= lcs[i - 1][j]))
		{

			strcpy(info->changed_lines[info->num_changes], new_content.lines[j - 1]);
			info->change_types[info->num_changes] = '+';
			info->num_changes++;
			info->lines_added++;
			j--;
		}
		else if (i > 0 && (j == 0 || lcs[i][j - 1] < lcs[i - 1][j]))
		{

			strcpy(info->changed_lines[info->num_changes], old_content.lines[i - 1]);
			info->change_types[info->num_changes] = '-';
			info->num_changes++;
			info->lines_removed++;
			i--;
		}
	}
}

void 
create_directory(const char *path)
{
#ifdef _WIN32
	_mkdir(path);
#else
	mkdir(path, 0755);
#endif
}

char *
get_username(void)
{
	char *username = getenv("USER");
	if (!username)
		username = getenv("USERNAME"); 
	return username ? username : "unknown";
}

void 
init(void)
{
	if (access(VCS_DIR, F_OK) == 0)
	{
		printf("%sRepository already exists!%s\n", YELLOW, RESET);
		return;
	}

	create_directory(VCS_DIR);
	create_directory(BACKUP_DIR);

	FILE *history = fopen(HISTORY_FILE, "w");
	if (!history)
	{
		printf("%sError creating history file%s\n", RED, RESET);
		return;
	}
	fclose(history);

	DIR *dir = opendir(".");
	struct dirent *entry;

	if (dir != NULL)
	{
		int files_added = 0;
		while ((entry = readdir(dir)) != NULL)
		{
			if (strcmp(entry->d_name, ".") == 0 ||
				strcmp(entry->d_name, "..") == 0 ||
				strcmp(entry->d_name, ".svcs") == 0)
			{
				continue;
			}

			struct stat path_stat;
			stat(entry->d_name, &path_stat);

			if (!S_ISREG(path_stat.st_mode))
			{
				continue;
			}

			char backup_path[MAX_PATH];
			snprintf(backup_path, sizeof(backup_path), "%s/%s.1", BACKUP_DIR,
					 entry->d_name);

			char command[MAX_PATH * 2];
			snprintf(command, sizeof(command), "cp %s %s", entry->d_name,
					 backup_path);
			system(command);

			EnhancedVersionInfo info = {0};
			strncpy(info.filename, entry->d_name, MAX_PATH - 1);
			strncpy(info.username, getenv("USER") ? getenv("USER") : "unknown",
					MAX_PATH - 1);
			info.timestamp = time(NULL);
			info.version = 1;

			FILE *history = fopen(HISTORY_FILE, "ab");
			if (history)
			{
				fwrite(&info, sizeof(EnhancedVersionInfo), 1, history);
				fclose(history);
			}
			printf(" %s+ %s%s\n", GREEN, entry->d_name, RESET);
			files_added++;
		}

		if (files_added == 0)
		{
			printf("%sInitialized empty repository%s\n", GREEN, RESET);
		}
		else
		{
			printf("%sInitialized repository with %d files%s\n", GREEN, files_added, RESET);
		}
	}
	closedir(dir);
}

void 
save(const char *filename)
{
	char latest_version[MAX_PATH];
	char prev_version[MAX_PATH];
	EnhancedVersionInfo info;
	int latest = 0;

	if (!is_tracked(filename))
	{
		printf("%sFile is not tracked. Use 'track' command first: %s%s\n",
			   RED, filename, RESET);
		return;
	}

	if (access(filename, F_OK) != 0)
	{
		printf("%sFile does not exist: %s%s\n", RED, filename, RESET);
		return;
	}

	FILE *history = fopen(HISTORY_FILE, "r");
	if (!history)
	{
		printf("%sNo history found%s\n", RED, RESET);
		return;
	}

	while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1)
		if (strcmp(info.filename, filename) == 0 && info.version > latest)
			latest = info.version;
	fclose(history);

	latest++;

	snprintf(latest_version, MAX_PATH, "%s/%s.%d", BACKUP_DIR, filename, latest);
	char command[MAX_PATH * 2];
	snprintf(command, sizeof(command), "cp %s %s", filename, latest_version);
	system(command);

	EnhancedVersionInfo new_info = {0};
	strncpy(new_info.filename, filename, MAX_PATH - 1);
	strncpy(new_info.username, getenv("USER") ? getenv("USER") : "unknown", MAX_PATH - 1);
	new_info.timestamp = time(NULL);
	new_info.version = latest;

	snprintf(prev_version, MAX_PATH, "%s/%s.%d", BACKUP_DIR, filename, latest - 1);
	compute_changes(prev_version, latest_version, &new_info);

	FILE *hist = fopen(HISTORY_FILE, "ab");
	if (hist)
	{
		fwrite(&new_info, sizeof(EnhancedVersionInfo), 1, hist);
		fclose(hist);
		printf("%sSaved version %d of %s%s\n", GREEN, latest, filename, RESET);
	}
}

void 
diff(const char *filename)
{
	char latest_version[MAX_PATH];
	char prev_version[MAX_PATH];
	EnhancedVersionInfo info;
	int latest = 0;

	FILE *history = fopen(HISTORY_FILE, "r");
	if (!history)
	{
		printf("%sNo history found%s\n", RED, RESET);
		return;
	}

	while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1)
		if (strcmp(info.filename, filename) == 0 && info.version > latest)
			latest = info.version;
	fclose(history);

	if (latest < 1)
	{
		printf("%sNo versions found for %s%s\n", YELLOW, filename, RESET);
		return;
	}

	snprintf(latest_version, MAX_PATH, "%s/%s.%d", BACKUP_DIR, filename, latest);
	diff_files(latest_version, filename);
}

void 
list_versions(const char *filename)
{
	EnhancedVersionInfo info;
	FILE *history = fopen(HISTORY_FILE, "r");

	if (!history)
	{
		printf("%sNo history found%s\n", RED, RESET);
		return;
	}

	printf("%sAvailable versions for %s:%s\n", YELLOW, filename, RESET);
	while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1)
	{
		if (strcmp(info.filename, filename) == 0)
		{
			char time_str[26];
			ctime_r(&info.timestamp, time_str);
			time_str[24] = '\0';

			printf("Version %s%d%s - %s\n", CYAN,
				   info.version, RESET, time_str);

			if (info.version > 1)
			{
				printf("Changes: %s+%d%s, %s-%d%s lines\n",
					   GREEN, info.lines_added, RESET,
					   RED, info.lines_removed, RESET);
			}
		}
	}
	fclose(history);
}

void 
revert(const char *filename, int target_version)
{
	EnhancedVersionInfo info;
	int latest = 0;
	int version_exists = 0;

	FILE *history = fopen(HISTORY_FILE, "r");
	if (!history)
	{
		printf("%sNo history found%s\n", RED, RESET);
		return;
	}

	while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1)
	{
		if (strcmp(info.filename, filename) == 0)
		{
			if (info.version > latest)
			{
				latest = info.version;
			}
			if (info.version == target_version)
			{
				version_exists = 1;
			}
		}
	}
	fclose(history);

	if (latest < 1)
	{
		printf("%sNo versions found for %s%s\n", YELLOW, filename, RESET);
		return;
	}

	if (target_version < 1 || target_version > latest)
	{
		printf("%sInvalid version number. Available versions: 1 to %d%s\n",
			   RED, latest, RESET);
		return;
	}

	if (!version_exists)
	{
		printf("%sVersion %d does not exist for %s%s\n",
			   RED, target_version, filename, RESET);
		return;
	}

	char version_path[MAX_PATH];
	snprintf(version_path, sizeof(version_path), "%s/%s.%d", BACKUP_DIR,
			 filename, target_version);
	char command[MAX_PATH * 2];
	snprintf(command, sizeof(command), "cp %s %s", version_path, filename);

	if (system(command) == 0)
	{
		printf("%sReverted %s to version %d%s\n", GREEN,
			   filename, target_version, RESET);
	}
	else
	{
		printf("%sError reverting to version %d%s\n", RED,
			   target_version, RESET);
	}
}

void 
history()
{
	EnhancedVersionInfo info;
	FILE *history = fopen(HISTORY_FILE, "rb");
	int i;

	if (!history)
	{
		printf("%sNo history found%s\n", RED, RESET);
		return;
	}

	printf("%sVersion History:%s\n", YELLOW, RESET);
	while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1)
	{
		char time_str[26];
		ctime_r(&info.timestamp, time_str);
		time_str[24] = '\0';

		int exists = file_exists(info.filename);

		printf("\n%sVersion %d%s - File: %s%s%s %s%s%s\n",
			   CYAN, info.version, RESET,
			   YELLOW, info.filename, RESET,
			   exists ? GREEN : RED,
			   exists ? "(exists)" : "(deleted)",
			   RESET);
		printf("By: %s at %s\n", info.username, time_str);

		if (info.version > 1)
		{
			printf("Changes: %s+%d%s, %s-%d%s lines\n",
				   GREEN, info.lines_added, RESET,
				   RED, info.lines_removed, RESET);

			printf("Modified lines:\n");
			for (i = 0; i < info.num_changes; i++)
			{
				if (info.change_types[i] == '+')
				{
					printf("%s+%s%s\n", GREEN, info.changed_lines[i], RESET);
				}
				else
				{
					printf("%s-%s%s\n", RED, info.changed_lines[i], RESET);
				}
			}
		}
		printf("\n");
	}
	fclose(history);
}

int main
(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("\n");
		printf("ew - simple version control\n");
		printf("===========================\n");
		printf("Usage: %s <command> [filename] [version]\n", argv[0]);
		printf("\n");
		printf("Commands:\n\v");
		printf("  init                 Create new repository\n");
		printf("  track <file>         Start tracking a file\n");
		printf("  untrack <file>       Stop tracking a file\n");
		printf("  status               List tracked files\n");
		printf("  find                 Find files in repository\n");
		printf("  diff <file>          Show changes\n");
		printf("  save <file>          Save changes\n");
		printf("  revert <file> [ver]  Revert to version\n");
		printf("  history              Show history\n");
		printf("\n");
		return 1;
	}

    Command cmd = CMD_UNKNOWN;
    if (strcmp(argv[1], "init") == 0)     cmd = CMD_INIT;
    if (strcmp(argv[1], "diff") == 0)     cmd = CMD_DIFF;
	if (strcmp(argv[1], "find") == 0)	  cmd = CMD_FIND;
    if (strcmp(argv[1], "save") == 0)     cmd = CMD_SAVE;
    if (strcmp(argv[1], "revert") == 0)   cmd = CMD_REVERT;
    if (strcmp(argv[1], "history") == 0)  cmd = CMD_HISTORY;
    if (strcmp(argv[1], "status") == 0)   cmd = CMD_STATUS;
    if (strcmp(argv[1], "track") == 0)    cmd = CMD_TRACK;
    if (strcmp(argv[1], "untrack") == 0)  cmd = CMD_UNTRACK;

    ErrorCode result = handle_command(cmd, argc, argv);
    
    if (result != SUCCESS) {
        switch (result) {
            case ERR_NO_REPO:
                PRINT_ERROR("No repository found");
                break;
            case ERR_NO_HISTORY:
                PRINT_ERROR("No history found");
                break;
            case ERR_NO_FILE:
                PRINT_ERROR("File not found");
                break;
            case ERR_INVALID_VERSION:
                PRINT_ERROR("Invalid version specified");
                break;
            case ERR_FILE_NOT_TRACKED:
                PRINT_ERROR("File is not tracked");
                break;
            case ERR_BINARY_FILE:
                PRINT_ERROR("Binary files are not supported");
                break;
			case ERR_UNKNOWN_COMMAND:
    			PRINT_ERROR("Unknown command: %s", argv[1]);
    			break;

            default:
                PRINT_ERROR("Unknown error occurred");
        }
        return 1;
    }

    return 0;
}
