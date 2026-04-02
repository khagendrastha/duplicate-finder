#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_FILES    1024   /* Maximum files the program can track        */
#define MAX_PATH_LEN     512    /* Maximum length of any file path            */
#define CHUNK_SIZE   4096   /* Bytes read at a time during hashing/copy   */

typedef struct {
    char          path[MAX_PATH_LEN];   /* Full source path                   */
    char          name[MAX_PATH_LEN];   /* File name only (for display/dest)  */
    unsigned long hash;             /* Rolling hash of file contents      */
    int           is_duplicate;     /* See flag meanings above            */
} FileInfo;

unsigned long compute_hash(const char *filepath)
{
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "  [WARNING] Cannot open for hashing: %s\n", filepath);
        return 0UL;
    }

    unsigned long  hash = 5381UL;           /* DJB2-style seed            */
    unsigned char  buf[CHUNK_SIZE];
    size_t         n;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        for (size_t i = 0; i < n; i++) {
            /* Rotate hash left by 5 bits, then XOR with the current byte */
            hash = ((hash << 5) | (hash >> (sizeof(unsigned long) * 8 - 5)))
                   ^ (unsigned long)buf[i];
        }
    }

    fclose(fp);
    return hash;
}

int scan_folder(const char *folder_path, FileInfo files[], int max_files)
{
    DIR           *dir;
    struct dirent *entry;
    struct stat    st;
    int            count = 0;

    dir = opendir(folder_path);
    if (!dir) {
        fprintf(stderr, "[ERROR] Cannot open directory: %s\n", folder_path);
        return -1;
    }

    printf("\n[SCAN] Scanning: %s\n", folder_path);
    printf("  %-42s %s\n", "Filename", "Hash");
    printf("  %-42s %s\n", "--------", "----");

    while ((entry = readdir(dir)) != NULL) {

        /* Skip the current-dir and parent-dir entries */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        /* Build the full path for this entry */
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path),
                 "%s/%s", folder_path, entry->d_name);

        /* stat() the entry -- skip anything that is not a regular file */
        if (stat(full_path, &st) != 0)   continue;
        if (!S_ISREG(st.st_mode))        continue;

        /* Guard against overflowing the files[] array */
        if (count >= max_files) {
            fprintf(stderr,
                    "[WARNING] File limit (%d) reached; "
                    "remaining files ignored.\n", max_files);
            break;
        }

        /* Store file information */
        strncpy(files[count].path, full_path,     MAX_PATH_LEN - 1);
        strncpy(files[count].name, entry->d_name, MAX_PATH_LEN - 1);
        files[count].path[MAX_PATH_LEN - 1] = '\0';
        files[count].name[MAX_PATH_LEN - 1] = '\0';
        files[count].is_duplicate = 0;   /* default: treat as unique */

        /* Compute and store the hash */
        files[count].hash = compute_hash(full_path);

        printf("  %-42s %lu\n", entry->d_name, files[count].hash);
        count++;
    }

    closedir(dir);
    printf("[SCAN] %d file(s) found.\n", count);
    return count;
}

static int copy_file(const char *src, const char *dst)
{
    FILE          *in  = fopen(src, "rb");
    FILE          *out = fopen(dst, "wb");
    unsigned char  buf[CHUNK_SIZE];
    size_t         n;

    if (!in || !out) {
        if (in)  fclose(in);
        if (out) fclose(out);
        return -1;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);

    fclose(in);
    fclose(out);
    return 0;
}

void process_files(FileInfo files[], int count, const char *new_folder)
{
    int i, j;
    printf("\n[DEDUPLICATE] Comparing file hashes...\n");

    for (i = 0; i < count; i++) {

        if (files[i].is_duplicate)
            continue;

        for (j = i + 1; j < count; j++) {
            if (files[i].hash == files[j].hash) {

                /* j is a redundant copy -- flag it, leave i as the rep  */
                files[j].is_duplicate = 1;

                printf("  [DUP FOUND]  \"%s\"  is a duplicate of  \"%s\""
                       "  (hash=%lu)\n",
                       files[j].name, files[i].name, files[i].hash);
            }
        }
    }

    printf("\n[COPY] Destination folder: %s\n", new_folder);

    int copied = 0;

    for (i = 0; i < count; i++) {

        if (files[i].is_duplicate)
            continue;   /* redundant copy -- skip */

        /* Build the full destination path */
        char dest[MAX_PATH_LEN];
        snprintf(dest, sizeof(dest), "%s/%s", new_folder, files[i].name);

        if (copy_file(files[i].path, dest) == 0) {
            printf("  [COPIED]  %-38s ->  %s\n", files[i].name, dest);
            copied++;
        } else {
            fprintf(stderr,
                    "  [ERROR]   Failed to copy \"%s\"\n", files[i].name);
        }
    }

    printf("[COPY] %d file(s) copied to new folder.\n", copied);

   
    printf("\n[REPORT] Duplicate files remaining in original folder:\n");

    int dup_count = 0;

    for (i = 0; i < count; i++) {
        if (files[i].is_duplicate) {
            printf("  [DUP]  %-42s  hash=%lu\n",
                   files[i].name, files[i].hash);
            dup_count++;
        }
    }

    if (dup_count == 0)
        printf("  (none -- no redundant duplicates found)\n");
    else
        printf("[REPORT] %d redundant duplicate(s) left in original folder.\n",
               dup_count);
}


int main(void)
{
    char original_folder[MAX_PATH_LEN];
    char new_folder[MAX_PATH_LEN];

  
    printf("========================================\n");
    printf("          File Deduplicator             \n");
    printf("========================================\n\n");

    printf("Enter original folder path : ");
    if (!fgets(original_folder, sizeof(original_folder), stdin)) {
        fprintf(stderr, "[ERROR] Could not read input.\n");
        return EXIT_FAILURE;
    }
    original_folder[strcspn(original_folder, "\r\n")] = '\0';

    printf("Enter new folder path      : ");
    if (!fgets(new_folder, sizeof(new_folder), stdin)) {
        fprintf(stderr, "[ERROR] Could not read input.\n");
        return EXIT_FAILURE;
    }
    new_folder[strcspn(new_folder, "\r\n")] = '\0';

  
#if defined(_WIN32) || defined(_WIN64)
    mkdir(new_folder);
#else
    mkdir(new_folder, 0755);
#endif

 

    int file_count = scan_folder(original_folder, files, MAX_FILES);
    if (file_count <= 0) {
        printf("\nNo files found in \"%s\". Exiting.\n", original_folder);
        return EXIT_SUCCESS;
    }

   
    process_files(files, file_count, new_folder);

    printf("\n[DONE] All operations complete.\n");
    return EXIT_SUCCESS;
}
