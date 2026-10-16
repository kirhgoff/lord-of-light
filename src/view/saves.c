#include "saves.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool fail(char *err, size_t err_size, const char *what, const char *path)
{
    if (err_size > 0) snprintf(err, err_size, "%s %s: %s", what, path, errno ? strerror(errno) : "invalid");
    return false;
}

static bool is_save_name(const char *name)
{
    size_t length = strlen(name);
    return length > 5 && strcmp(name + length - 5, ".json") == 0;
}

void saves_path_for(const char *dir, time_t when, char *out, size_t size)
{
    char stamp[32];
    struct tm local;
    localtime_r(&when, &local);
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &local);
    snprintf(out, size, "%s/%s.json", dir, stamp);
}

bool saves_newest(const char *dir, char *out, size_t size, char *err, size_t err_size)
{
    errno = 0;
    DIR *folder = opendir(dir);
    if (!folder) return fail(err, err_size, "cannot open", dir);
    char newest[256] = "";
    for (struct dirent *entry; (entry = readdir(folder));)
        if (is_save_name(entry->d_name) && strcmp(entry->d_name, newest) > 0) snprintf(newest, sizeof newest, "%s", entry->d_name);
    closedir(folder);
    if (!newest[0]) {
        if (err_size > 0) snprintf(err, err_size, "no saves in %s", dir);
        return false;
    }
    snprintf(out, size, "%s/%s", dir, newest);
    return true;
}

bool saves_write(const char *dir, time_t when, const Battle *battle, const GameData *data, char *path, size_t path_size,
                 char *err, size_t err_size)
{
    errno = 0;
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) return fail(err, err_size, "cannot create", dir);
    saves_path_for(dir, when, path, path_size);
    char *json = battle_save_json(battle, data);
    FILE *file = json ? fopen(path, "w") : NULL;
    bool ok = file && fputs(json, file) >= 0;
    if (file && fclose(file) != 0) ok = false;
    free(json);
    return ok || fail(err, err_size, "cannot write", path);
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *text = NULL;
    long length = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (length >= 0 && fseek(file, 0, SEEK_SET) == 0 && (text = malloc((size_t)length + 1))) {
        if (fread(text, 1, (size_t)length, file) == (size_t)length) text[length] = '\0';
        else free(text), text = NULL;
    }
    fclose(file);
    return text;
}

bool saves_load_newest(const char *dir, Battle *battle, const GameData *data, char *path, size_t path_size, char *err,
                       size_t err_size)
{
    if (!saves_newest(dir, path, path_size, err, err_size)) return false;
    errno = 0;
    char *json = read_file(path);
    if (!json) return fail(err, err_size, "cannot read", path);
    bool ok = battle_load_json(battle, data, json, err, err_size);
    free(json);
    return ok;
}
