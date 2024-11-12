#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* macros */
#define MAX_LINES	1000
#define MAX_LINE_LENGTH	256
#define MAX_PATH	1024
#define MAX_COMMAND	128
#define VCS_DIR		".svcs"
#define HISTORY_FILE	".svcs/history"
#define BACKUP_DIR	".svcs/versions"

/* colors */
#define RED	"\x1b[31m"
#define GREEN	"\x1b[32m"
#define YELLOW	"\x1b[33m"
#define RESET	"\x1b[0m"
#define CYAN	"\x1b[36m"

/* types */
typedef struct {
	char lines[MAX_LINES][MAX_LINE_LENGTH];
	int line_count;
} FileContents;

typedef struct {
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


/* function declarations */
static void compute_changes(const char* old_file, const char* new_file, EnhancedVersionInfo* info);
static void create_directory(const char *path);
static void create_patch(const char *filename);
static void diff(const char *filename);
static void diff_files(const char* file1, const char* file2);
static int file_exists(const char *filename);
static void history(void);
static void init(void);
static void revert(const char *filename);
static void save(const char *filename);
char *get_username(void);
FileContents read_file(const char *filename);


static void
diff_files(const char* file1, const char* file2)
{
    FileContents old_content = read_file(file1);
    FileContents new_content = read_file(file2);

    printf("%s--- %s%s\n", RED, file1, RESET);
    printf("%s+++ %s%s\n", GREEN, file2, RESET);

    int i = 0, j = 0;
    int chunk_start = -1;
    int in_chunk = 0;

    while (i < old_content.line_count || j < new_content.line_count) {
        if (i < old_content.line_count && j < new_content.line_count && 
            strcmp(old_content.lines[i], new_content.lines[j]) == 0) {
            if (in_chunk) {
                printf("%s@@ -%d,%d +%d,%d @@%s\n", 
                       CYAN, chunk_start + 1, i - chunk_start,
                       chunk_start + 1, j - chunk_start, RESET);
                in_chunk = 0;
            }
            i++;
            j++;
            continue;
        }

        if (!in_chunk) {
            chunk_start = i;
            in_chunk = 1;
        }

        if (i < old_content.line_count && 
            (j >= new_content.line_count || 
             (j < new_content.line_count && strcmp(old_content.lines[i], new_content.lines[j]) != 0))) {
            printf("%s-%s%s\n", RED, old_content.lines[i], RESET);
            i++;
        }
        
        if (j < new_content.line_count && 
            (i >= old_content.line_count || 
             (i < old_content.line_count && strcmp(old_content.lines[i], new_content.lines[j]) != 0))) {
            printf("%s+%s%s\n", GREEN, new_content.lines[j], RESET);
            j++;
        }
    }

    if (in_chunk) {
        printf("%s@@ -%d,%d +%d,%d @@%s\n", 
               CYAN, chunk_start + 1, i - chunk_start,
               chunk_start + 1, j - chunk_start, RESET);
    }
}

static int
file_exists(const char *filename)
{
    return access(filename, F_OK) == 0;
}

FileContents read_file
(const char *filename) {
	FileContents content = {{{0}}, 0};
	FILE *file = fopen(filename, "r");
	if (!file)
		return content;

	while (content.line_count < MAX_LINES &&
	       fgets(content.lines[content.line_count], MAX_LINE_LENGTH, file)) {
		char *newline = strchr(content.lines[content.line_count], '\n');
		if (newline)
			*newline = '\0';
		content.line_count++;
	}

	fclose(file);
	return content;
}

/* needed for history, might change it later */
static void 
compute_changes (const char* old_file, const char* new_file, EnhancedVersionInfo* info) {
    FileContents old_content = read_file(old_file);
    FileContents new_content = read_file(new_file);
    
    info->lines_added = 0;
    info->lines_removed = 0;
    info->num_changes = 0;
    
    int lcs[MAX_LINES + 1][MAX_LINES + 1] = {{0}};
    
    /* Build LCS matrix with O(mn) complexity. Alternative Myers Algorithm*/
    for (int i = 1; i <= old_content.line_count; i++) {
        for (int j = 1; j <= new_content.line_count; j++) {
            if (strcmp(old_content.lines[i-1], new_content.lines[j-1]) == 0) {
                lcs[i][j] = lcs[i-1][j-1] + 1;
            } else {
                lcs[i][j] = (lcs[i-1][j] > lcs[i][j-1]) ? lcs[i-1][j] : lcs[i][j-1];
            }
        }
    }
    
    int i = old_content.line_count;
    int j = new_content.line_count;
    
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && strcmp(old_content.lines[i-1], new_content.lines[j-1]) == 0) {
            i--;
            j--;
        } else if (j > 0 && (i == 0 || lcs[i][j-1] >= lcs[i-1][j])) {

            strcpy(info->changed_lines[info->num_changes], new_content.lines[j-1]);
            info->change_types[info->num_changes] = '+';
            info->num_changes++;
            info->lines_added++;
            j--;

        } else if (i > 0 && (j == 0 || lcs[i][j-1] < lcs[i-1][j])) {

            strcpy(info->changed_lines[info->num_changes], old_content.lines[i-1]);
            info->change_types[info->num_changes] = '-';
            info->num_changes++;
            info->lines_removed++;
            i--;
        }
    }
}

static void 
create_directory(const char *path)
{
#ifdef _WIN32
	_mkdir(path);
#else
	mkdir(path, 0755);
#endif
}

/* for history, we want a username */
char *get_username(void) 
{
	char *username = getenv("USER");
	if (!username)
		username = getenv("USERNAME");  /* For Windows */
	return username ? username : "unknown";
}

static void 
init(void) 
{   
	if (access(VCS_DIR, F_OK) == 0) {
		printf("%sRepository already exists!%s\n", YELLOW, RESET);
		return;
	}

	create_directory(VCS_DIR);
	create_directory(BACKUP_DIR);

    FILE *history = fopen(HISTORY_FILE,"w");
    if(!history){
        printf("%sError creating history file%s\n",RED,RESET);
        return;
    }
    fclose(history);

    /* add all files of the current dir into versions*/
    DIR *dir = opendir(".");
    struct dirent *entry;

    if(dir != NULL){
        int files_added = 0;
        while((entry = readdir(dir)) != NULL){
            if(strcmp(entry->d_name,".") == 0 ||
            strcmp(entry->d_name,"..") == 0 ||
            strcmp(entry->d_name,".svcs") == 0){
                continue;
            }

            struct stat path_stat;
            stat(entry->d_name, &path_stat);
            if(!S_ISREG(path_stat.st_mode)){
                continue;
            }

            char backup_path[MAX_PATH];
            snprintf(backup_path,sizeof(backup_path),"%s/%s.1",BACKUP_DIR,
                    entry->d_name);

            char command[MAX_PATH * 2];
            snprintf(command, sizeof(command),"cp %s %s", entry->d_name,
                    backup_path);
            system(command);

            /* write version 1 to history*/
            EnhancedVersionInfo info = {0};
            strncpy(info.filename, entry->d_name, MAX_PATH -1);
            strncpy(info.username, getenv("USER")?getenv("USER"): "unknown",
                    MAX_PATH-1);
            info.timestamp = time(NULL);
            info.version = 1;

            FILE *history = fopen(HISTORY_FILE, "ab");
            if(history){
                fwrite(&info, sizeof(EnhancedVersionInfo),1,history);
                fclose(history);
            }
            printf(" %s+ %s%s\n", GREEN, entry->d_name, RESET);
            files_added++;
        }

        if(files_added == 0){
            printf("%sInitialized empty repository%s\n",GREEN, RESET);
        } else {
            printf("%sInitialized repository with %d files%s\n", GREEN, files_added, RESET);
        }
    }
        closedir(dir);
}

static void
save(const char *filename)
{
    char latest_version[MAX_PATH];
    char prev_version[MAX_PATH];
    EnhancedVersionInfo info;
    int latest = 0;

    if(access(filename, F_OK) != 0){
        printf("%sFile does not exist: %s%s\n", RED, filename, RESET);
        return;
    }

    FILE *history = fopen(HISTORY_FILE, "r");
    if(!history){
        printf("%sNo history found%s\n", RED, RESET);
            return;
    }

    while(fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1)
        if(strcmp(info.filename, filename) == 0 && info.version > latest)
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
    if (hist) {
        fwrite(&new_info, sizeof(EnhancedVersionInfo), 1, hist);
        fclose(hist);
        printf("%sSaved version %d of %s%s\n", GREEN, latest, filename, RESET);
    }
}

static void
diff(const char *filename)
{
    char latest_version[MAX_PATH];
    char prev_version[MAX_PATH];
    EnhancedVersionInfo info;
    int latest = 0;

    FILE *history = fopen(HISTORY_FILE, "r");
    if (!history) {
        printf("%sNo history found%s\n", RED, RESET);
        return;
    }

    while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1)
        if (strcmp(info.filename, filename) == 0 && info.version > latest)
            latest = info.version;
    fclose(history);

    if (latest < 1) {
        printf("%sNo versions found for %s%s\n", YELLOW, filename, RESET);
        return;
    }

    snprintf(latest_version, MAX_PATH, "%s/%s.%d", BACKUP_DIR, filename, latest);
    diff_files(latest_version, filename);

}

static void 
revert(const char *filename) 
{
	EnhancedVersionInfo info;
	int latest = 0;
	char version_path[MAX_PATH];
	char command[MAX_PATH * 2];
	FILE *history = fopen(HISTORY_FILE, "r");

	if (!history) {
		printf("%sNo history found%s\n", RED, RESET);
		return;
	}

	while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1)
		if (strcmp(info.filename, filename) == 0 && info.version > latest)
			latest = info.version;
	fclose(history);

	if (latest < 1) {
		printf("%sNo versions found to revert%s\n", YELLOW, RESET);
		return;
	}

	snprintf(version_path, MAX_PATH, "%s/%s.%d", BACKUP_DIR, filename,
		 latest - 1);
	snprintf(command, sizeof(command), "cp %s %s", version_path, filename);
	system(command);

	printf("%sReverted to version %d%s\n", GREEN, latest - 1, RESET);
}

static void 
history() 
{
    EnhancedVersionInfo info;
    FILE *history = fopen(HISTORY_FILE, "rb");
    
    if (!history) {
        printf("%sNo history found%s\n", RED, RESET);
        return;
    }
    
    printf("%sVersion History:%s\n", YELLOW, RESET);
    while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1) {
        char time_str[26];
        ctime_r(&info.timestamp, time_str);
        time_str[24] = '\0';  
        
        int exists = file_exists(info.filename);
        
        printf("\n%sVersion %d%s - File: %s%s%s %s%s%s\n",
               CYAN, info.version, RESET,
               YELLOW, info.filename, RESET,
               exists ? GREEN:RED,
               exists ? "(exists)":"(deleted)",
               RESET);
        printf("By: %s at %s\n", info.username, time_str);
        
        if (info.version > 1) {  
            printf("Changes: %s+%d%s, %s-%d%s lines\n",
                   GREEN, info.lines_added, RESET,
                   RED, info.lines_removed, RESET);
                   
            printf("Modified lines:\n");
            for (int i = 0; i < info.num_changes; i++) {
                if (info.change_types[i] == '+') {
                    printf("%s+%s%s\n", GREEN, info.changed_lines[i], RESET);
                } else {
                    printf("%s-%s%s\n", RED, info.changed_lines[i], RESET);
                }
            }
        }
        printf("\n");
    }
    
    fclose(history);
}

static void 
create_patch(const char *filename) 
{
    char latest_version[MAX_PATH];
    char prev_version[MAX_PATH];
    EnhancedVersionInfo info;
    int latest = 0;
    time_t current_time;
    char time_str[26];

    FILE *history = fopen(HISTORY_FILE, "r");
    if (!history) {
        printf("%sNo history found%s\n", RED, RESET);
        return;
    }

    while (fread(&info, sizeof(EnhancedVersionInfo), 1, history) == 1) {
        if (strcmp(info.filename, filename) == 0 
			&& info.version > latest) {
            latest = info.version;
        }
    }
    fclose(history);

    if (latest < 1) {
        printf("%sNo versions found for %s%s\n", 
				YELLOW, filename, RESET);
        return;
    }

    current_time = time(NULL);
    ctime_r(&current_time, time_str);
    time_str[24] = '\0';  

    printf("--- %s.old	%s\n", filename, time_str);
    printf("+++ %s	%s\n", filename, time_str);

    snprintf(latest_version, MAX_PATH, "%s/%s.%d", 
			BACKUP_DIR, filename, latest);
    
    FileContents old_content;
    FileContents new_content;

    if (latest == 1) {
        old_content = read_file(filename);
        new_content = read_file(latest_version);
    } else {
        snprintf(prev_version, MAX_PATH, "%s/%s.%d", 
		BACKUP_DIR, filename, latest - 1);
        old_content = read_file(prev_version);
        new_content = read_file(latest_version);
    }

    int lcs[MAX_LINES + 1][MAX_LINES + 1] = {{0}};
    
    for (int i = 1; i <= old_content.line_count; i++) {
        for (int j = 1; j <= new_content.line_count; j++) {
            if (strcmp(old_content.lines[i-1], 
			new_content.lines[j-1]) == 0) {
                lcs[i][j] = lcs[i-1][j-1] + 1;
            } else {
                lcs[i][j] = (lcs[i-1][j] > lcs[i][j-1]) 
				? lcs[i-1][j] : lcs[i][j-1];
            }
        }
    }

    int context_lines = 3;
    int changes_start = -1;
    int old_start = 0;
    int new_start = 0;
    int old_changes = 0;
    int new_changes = 0;
    char context_buffer[MAX_LINES][MAX_LINE_LENGTH];
    int context_count = 0;

    int i = 0, j = 0;
    while (i < old_content.line_count || j < new_content.line_count) {
        if (i < old_content.line_count && j < new_content.line_count &&
            strcmp(old_content.lines[i], new_content.lines[j]) == 0) {

            if (changes_start != -1) {
                if (context_count == context_lines) {

                    printf("@@ -%d,%d +%d,%d @@\n", 
                           changes_start + 1, old_changes + context_lines,
                           changes_start + 1, new_changes + context_lines);
                    
                    for (int k = 0; k < context_lines; k++) {
                        printf(" %s\n", context_buffer[k]);
                    }
                    
                    for (int k = old_start; k < i; k++) {
                        printf("-%s\n", old_content.lines[k]);
                    }
                    for (int k = new_start; k < j; k++) {
                        printf("+%s\n", new_content.lines[k]);
                    }
                    
                    changes_start = -1;
                    old_changes = new_changes = 0;
                    context_count = 0;
                }
                strcpy(context_buffer[context_count++], old_content.lines[i]);
            } else {

                strcpy(context_buffer[context_count++], old_content.lines[i]);

                if (context_count > context_lines) {

                    for (int k = 0; k < context_lines - 1; k++) {
                        strcpy(context_buffer[k], context_buffer[k + 1]);
                    }
                    context_count = context_lines;
                }
            }
            i++;
            j++;
        } else {

            if (changes_start == -1) {
                changes_start = (i > context_lines) ? i - context_lines : 0;
                old_start = i;
                new_start = j;
                context_count = 0;
            }
            
            if (i < old_content.line_count && 
                (j >= new_content.line_count || lcs[i+1][j] >= lcs[i][j+1])) {
                old_changes++;
                i++;
            } else if (j < new_content.line_count) {
                new_changes++;
                j++;
            }
        }
    }

    if (changes_start != -1) {
        printf("@@ -%d,%d +%d,%d @@\n", 
               changes_start + 1, old_changes + context_lines,
               changes_start + 1, new_changes + context_lines);
        
        for (int k = 0; k < context_lines && k < context_count; k++) {
            printf(" %s\n", context_buffer[k]);
        }
        
        for (int k = old_start; k < old_content.line_count; k++) {
            printf("-%s\n", old_content.lines[k]);
        }
        for (int k = new_start; k < new_content.line_count; k++) {
            printf("+%s\n", new_content.lines[k]);
        }
    }
}

int
main(int argc, char *argv[]) 
{
    if (argc < 2) {
        printf("Usage: %s <command> [filename]\n", argv[0]);
        printf("Commands: init, diff <file>, revert <file>, history\n");
        return 1;
    }
    if (strcmp(argv[1], "init") == 0) {
        init();
    } else if (strcmp(argv[1], "diff") == 0) {
        if (argc < 3) {
            printf("%sError: Missing filename%s\n", RED, RESET);
            return 1;
        }
        diff(argv[2]);
    } else if (strcmp(argv[1], "save") == 0){
        if (argc < 3){
            printf("%sError: Missing filename%s\n", RED, RESET);
            return 1;
        }
        save(argv[2]);
    } else if (strcmp(argv[1], "revert") == 0) {
        if (argc < 3) {
            printf("%sError: Missing filename%s\n", RED, RESET);
            return 1;
        }
        revert(argv[2]);
    } else if (strcmp(argv[1], "history") == 0) {
        history();
    } else {
        printf("%sUnknown command: %s%s\n", RED, argv[1], RESET);
        return 1;
    }

    return 0;
}
