#include <quanta/vfs.h>

static const struct quanta_ramfs_entry entries[] = {
    { "/etc/motd", "Welcome to QuantaOs.\n" },
    { "/etc/os-release", "NAME=QuantaOs\nARCH=x86_64\n" },
    { "/home/quanta/readme", "This is your single-user QuantaOs session.\n" },
    { "/bin/session", "ELF64 user session\n" },
    { "/bin/echo", "ELF64 utility metadata\n" },
    { "/bin/cat", "ELF64 utility metadata\n" },
};
static int same(const char *a, const char *b) { while (*a && *b && *a == *b) { ++a; ++b; } return *a == *b; }
int quanta_ramfs_read(const char *path, const char **contents) {
    unsigned index;
    for (index = 0; index < sizeof(entries) / sizeof(entries[0]); ++index) {
        if (same(path, entries[index].path)) { *contents = entries[index].contents; return 0; }
    }
    return -1;
}
const char *quanta_ramfs_list(const char *path) {
    if (same(path, "/") || same(path, "")) return "etc  home  bin\n";
    if (same(path, "/etc")) return "motd  os-release\n";
    if (same(path, "/home/quanta")) return "readme\n";
    if (same(path, "/bin")) return "cat  echo  session\n";
    return 0;
}
