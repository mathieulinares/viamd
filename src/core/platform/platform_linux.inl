#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <core/string_utils.h>

static DynamicArray<CString> curr_filters;

static int filter_func(const struct dirent *dir) {
    switch (dir->d_type) {
        case DT_REG:
        case DT_LNK:
            {
                const char *s = dir->d_name;
                int s_len = strlen(s);
                for (const auto& f : curr_filters) {
                    int len = s_len - f.count - 1;
                    if (len >= 0) {
                        StringBuffer<32> ext = f;
                        StringBuffer<32> buf;
                        snprintf(buf.beg(), 32, ".%s", ext.beg());
                        if (compare(s + len, buf, true)) return 1;
                    }
                }
            }
            break;
        default:
        break; 
    }

    return 0;
}

DynamicArray<DirEntry> list_directory(CString dir_path) {
    struct dirent **files;
    int i;
    int n;

    DynamicArray<DirEntry> res;


    n = scandir (dir_path, &files, NULL, alphasort);

    if (n >= 0) {
        /* Loop through file names */
        for (i = 0; i < n; i++) {
            struct dirent *ent;

            /* Get pointer to file entry */
            ent = files[i];

            DirEntry entry;

            /* Output file name */
            switch (ent->d_type) {
            case DT_REG:
                entry.type = DirEntry::File;
                //printf ("%s\n", ent->d_name);
                break;

            case DT_DIR:
                entry.type = DirEntry::Dir;
                //printf ("%s/\n", ent->d_name);
                break;

            case DT_LNK:
                entry.type = DirEntry::Link;
                //printf ("%s@\n", ent->d_name);
                break;

            default:
                entry.type = DirEntry::Unknown;
                //printf ("%s*\n", ent->d_name);
            }
            strncpy(entry.name.beg(), ent->d_name, 512);

            res.push_back(entry);
        }

        /* Release file names */
        for (i = 0; i < n; i++) {
            free (files[i]);
        }
        free (files);

    } else {
        StringBuffer<512> buf = dir_path;
        printf ("Cannot open directory %s\n", buf.beg());
    }

    return res;
}

CString get_cwd() {
    return { getcwd(path_cwd.beg(), 512) };
}

void sleep(int32 milliseconds) {
	usleep(milliseconds * 1000);
}