// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "vfs.h"
#include "memory_manager.h"
#include "spinlock.h"
#include <stddef.h>
#include "disk.h"

// ============================================================================
// VFS Mount Table and File Handle Pool
// ============================================================================

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int mount_count = 0;
static vfs_file_t open_files[VFS_MAX_OPEN_FILES];
static spinlock_t vfs_lock = SPINLOCK_INIT;

extern void serial_write(const char *str);
extern void serial_write_num(uint64_t num);

// ============================================================================
// String helpers (freestanding — no libc)
// ============================================================================

static int vfs_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void vfs_strcpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

static int vfs_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int vfs_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static bool vfs_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return false;
    }
    return true;
}

// ============================================================================
// Path Normalization
// ============================================================================

void vfs_normalize_path(const char *path, char *normalized) {
    // Resolve . and .. components, remove duplicate slashes
    char parts[32][128];
    int depth = 0;
    int i = 0;

    // Skip leading slash
    if (path[0] == '/') i = 1;

    while (path[i]) {
        // Skip duplicate slashes
        if (path[i] == '/') { i++; continue; }

        // Extract component
        int j = 0;
        while (path[i] && path[i] != '/' && j < 127) {
            parts[depth][j++] = path[i++];
        }
        parts[depth][j] = 0;

        if (parts[depth][0] == '.' && parts[depth][1] == 0) {
            // "." — skip
        } else if (parts[depth][0] == '.' && parts[depth][1] == '.' && parts[depth][2] == 0) {
            // ".." — go up
            if (depth > 0) depth--;
        } else {
            depth++;
            if (depth >= 32) break;
        }

        if (path[i] == '/') i++;
    }

    // Reconstruct
    normalized[0] = '/';
    int pos = 1;
    for (int k = 0; k < depth; k++) {
        int l = 0;
        while (parts[k][l] && pos < VFS_MAX_PATH - 2) {
            normalized[pos++] = parts[k][l++];
        }
        if (k < depth - 1) normalized[pos++] = '/';
    }
    normalized[pos] = 0;

    // Ensure root is just "/"
    if (pos == 1 && normalized[0] == '/') {
        normalized[1] = 0;
    }
}

// ============================================================================
// Mount Resolution — find the longest-prefix mount for a path
// ============================================================================

static vfs_mount_t* vfs_resolve_mount(const char *path, const char **rel_path_out) {
    vfs_mount_t *best = NULL;
    int best_len = -1;

    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;

        int mlen = mounts[i].path_len;

        // Root mount "/" matches everything
        if (mlen == 1 && mounts[i].path[0] == '/') {
            if (best_len < 1) {
                best = &mounts[i];
                best_len = 1;
            }
            continue;
        }

        // Check if path starts with this mount point
        if (vfs_strncmp(path, mounts[i].path, mlen) == 0) {
            // Must be followed by '/' or end of string
            if (path[mlen] == '/' || path[mlen] == '\0') {
                if (mlen > best_len) {
                    best = &mounts[i];
                    best_len = mlen;
                }
            }
        }
    }

    if (best && rel_path_out) {
        const char *rel = path + best_len;
        // Skip leading slash in relative path
        while (*rel == '/') rel++;
        *rel_path_out = rel;
    }

    return best;
}

// ============================================================================
// File Handle Pool
// ============================================================================

static vfs_file_t* vfs_alloc_file(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!open_files[i].valid) {
            open_files[i].valid = true;
            return &open_files[i];
        }
    }
    return NULL;
}

static void vfs_free_file(vfs_file_t *f) {
    if (f) {
        f->valid = false;
        f->fs_handle = NULL;
        f->mount = NULL;
    }
}

// ============================================================================
// Initialization
// ============================================================================

void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = false;
    }
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        open_files[i].valid = false;
    }
    mount_count = 0;

    serial_write("[VFS] Virtual File System initialized\n");
}

// ============================================================================
// Mount / Unmount
// ============================================================================

bool vfs_mount(const char *mount_path, const char *device, const char *fs_type,
               vfs_fs_ops_t *ops, void *fs_private) {
    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    if (mount_count >= VFS_MAX_MOUNTS) {
        spinlock_release_irqrestore(&vfs_lock, flags);
        serial_write("[VFS] ERROR: Mount table full\n");
        return false;
    }

    // Check for duplicate mount
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_strcmp(mounts[i].path, mount_path) == 0) {
            spinlock_release_irqrestore(&vfs_lock, flags);
            serial_write("[VFS] ERROR: Mount point already in use: ");
            serial_write(mount_path);
            serial_write("\n");
            return false;
        }
    }

    vfs_mount_t *m = &mounts[mount_count];
    vfs_strcpy(m->path, mount_path);
    m->path_len = vfs_strlen(mount_path);
    m->ops = ops;
    m->fs_private = fs_private;
    vfs_strcpy(m->device, device ? device : "none");
    vfs_strcpy(m->fs_type, fs_type ? fs_type : "unknown");
    m->active = true;
    mount_count++;

    spinlock_release_irqrestore(&vfs_lock, flags);

    serial_write("[VFS] Mounted ");
    serial_write(fs_type);
    serial_write(" (");
    serial_write(device ? device : "none");
    serial_write(") at ");
    serial_write(mount_path);
    serial_write("\n");

    return true;
}

bool vfs_umount(const char *mount_path) {
    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_strcmp(mounts[i].path, mount_path) == 0) {
            // Close any open files on this mount
            for (int j = 0; j < VFS_MAX_OPEN_FILES; j++) {
                if (open_files[j].valid && open_files[j].mount == &mounts[i]) {
                    if (mounts[i].ops->close) {
                        mounts[i].ops->close(mounts[i].fs_private, open_files[j].fs_handle);
                    }
                    vfs_free_file(&open_files[j]);
                }
            }

            serial_write("[VFS] Unmounted ");
            serial_write(mounts[i].path);
            serial_write("\n");

            mounts[i].active = false;

            // Compact array
            for (int k = i; k < mount_count - 1; k++) {
                mounts[k] = mounts[k + 1];
            }
            mount_count--;

            spinlock_release_irqrestore(&vfs_lock, flags);
            return true;
        }
    }

    spinlock_release_irqrestore(&vfs_lock, flags);
    return false;
}

// ============================================================================
// File Operations
// ============================================================================

vfs_file_t* vfs_open(const char *path, const char *mode) {
    if (!path || !mode) return NULL;

    // Normalize path
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized);

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount || !mount->ops->open) {
        spinlock_release_irqrestore(&vfs_lock, flags);
        return NULL;
    }

    // If rel_path is empty, use root
    if (!rel_path || rel_path[0] == '\0') {
        rel_path = "/";
    }

    vfs_file_t *vf = vfs_alloc_file();
    if (!vf) {
        spinlock_release_irqrestore(&vfs_lock, flags);
        serial_write("[VFS] ERROR: No free file handles\n");
        return NULL;
    }

    vf->mount = mount;

    // Release lock before calling FS ops (FS has its own locking)
    spinlock_release_irqrestore(&vfs_lock, flags);

    void *fs_handle = mount->ops->open(mount->fs_private, rel_path, mode);
    if (!fs_handle) {
        flags = spinlock_acquire_irqsave(&vfs_lock);
        vfs_free_file(vf);
        spinlock_release_irqrestore(&vfs_lock, flags);
        return NULL;
    }

    vf->fs_handle = fs_handle;
    return vf;
}

void vfs_close(vfs_file_t *file) {
    if (!file || !file->valid) return;

    vfs_mount_t *mount = file->mount;
    if (mount && mount->ops->close) {
        mount->ops->close(mount->fs_private, file->fs_handle);
    }

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
    vfs_free_file(file);
    spinlock_release_irqrestore(&vfs_lock, flags);
}

int vfs_read(vfs_file_t *file, void *buf, int size) {
    if (!file || !file->valid || !file->mount) return -1;
    if (!file->mount->ops->read) return -1;
    
    return file->mount->ops->read(file->mount->fs_private, file->fs_handle, buf, size);
}

int vfs_write(vfs_file_t *file, const void *buf, int size) {
    if (!file || !file->valid || !file->mount) return -1;
    if (!file->mount->ops->write) return -1;

    return file->mount->ops->write(file->mount->fs_private, file->fs_handle, buf, size);
}

int vfs_seek(vfs_file_t *file, int offset, int whence) {
    if (!file || !file->valid || !file->mount) return -1;
    if (!file->mount->ops->seek) return -1;
    return file->mount->ops->seek(file->mount->fs_private, file->fs_handle, offset, whence);
}

uint32_t vfs_file_position(vfs_file_t *file) {
    if (!file || !file->valid || !file->mount) return 0;
    if (!file->mount->ops->get_position) return 0;
    return file->mount->ops->get_position(file->fs_handle);
}

uint32_t vfs_file_size(vfs_file_t *file) {
    if (!file || !file->valid || !file->mount) return 0;
    if (!file->mount->ops->get_size) return 0;
    return file->mount->ops->get_size(file->fs_handle);
}

// ============================================================================
// Directory Operations
// ============================================================================

int vfs_list_directory(const char *path, vfs_dirent_t *entries, int max) {
    if (!path || !entries) return -1;
    

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized);

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    
    int count = 0;
    if (mount && mount->ops->readdir) {
        if (!rel_path || rel_path[0] == '\0') rel_path = "/";
        count = mount->ops->readdir(mount->fs_private, rel_path, entries, max);
        if (count < 0) count = 0; // Treat as virtual if readdir fails
    }

    // Merge in other mount points that are direct children of this path
    uint64_t v_flags = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;
        if (vfs_strcmp(mounts[i].path, normalized) == 0) continue; // Skip ourselves

        // Check if mount path starts with current path
        if (vfs_starts_with(mounts[i].path, normalized)) {
            const char *sub = mounts[i].path + vfs_strlen(normalized);
            if (*sub == '/') sub++; // skip slash

            if (*sub != '\0') {
                // Extract first component (direct child name)
                char comp[VFS_MAX_NAME];
                int j = 0;
                while (sub[j] && sub[j] != '/' && j < VFS_MAX_NAME - 1) {
                    comp[j] = sub[j];
                    j++;
                }
                comp[j] = 0;

                // Check if already in results
                bool found = false;
                for (int k = 0; k < count; k++) {
                    if (vfs_strcmp(entries[k].name, comp) == 0) {
                        found = true;
                        break;
                    }
                }

                if (!found && count < max) {
                    vfs_strcpy(entries[count].name, comp);
                    entries[count].is_directory = 1;
                    entries[count].size = 0;
                    entries[count].start_cluster = 0;
                    count++;
                }
            }
        }
    }
    spinlock_release_irqrestore(&vfs_lock, v_flags);

    // Special case: Ensure "dev" is visible in "/"
    if (vfs_strcmp(normalized, "/") == 0) {
        bool found_dev = false;
        for (int i = 0; i < count; i++) {
            if (vfs_strcmp(entries[i].name, "dev") == 0) {
                found_dev = true;
                break;
            }
        }
        if (!found_dev && count < max) {
            vfs_strcpy(entries[count].name, "dev");
            entries[count].is_directory = 1;
            entries[count].size = 0;
            entries[count].start_cluster = 0;
            count++;
        }
    }

    // Special case: /dev listing for block devices
    if (vfs_strcmp(normalized, "/dev") == 0) {
        int dcount = disk_get_count();
        for (int i = 0; i < dcount && count < max; i++) {
            Disk *d = disk_get_by_index(i);
            if (d) {
                bool found = false;
                for (int k = 0; k < count; k++) {
                    if (vfs_strcmp(entries[k].name, d->devname) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    vfs_strcpy(entries[count].name, d->devname);
                    entries[count].size = d->total_sectors * 512;
                    entries[count].is_directory = d->is_partition ? 1 : 0;
                    entries[count].start_cluster = 0;
                    entries[count].write_date = 0;
                    entries[count].write_time = 0;
                    count++;
                }
            }
        }
    }

    return count;
}

bool vfs_mkdir(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized);

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    // If it's in /dev/, check if it's within a mounted volume deeper than the device node
    if (vfs_starts_with(normalized, "/dev/")) {
        if (!mount || !rel_path || rel_path[0] == '\0') {
            return false; // Protect raw device nodes
        }
    }

    if (!mount || !mount->ops->mkdir) return false;
    return mount->ops->mkdir(mount->fs_private, rel_path);
}

bool vfs_rmdir(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized);

    // Protect root and virtual /dev directory itself
    if (normalized[0] == '/' && normalized[1] == '\0') return false;
    if (vfs_strcmp(normalized, "/dev") == 0) return false;

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    // If it's in /dev/, allow only if it's inside a mount beyond the device node
    if (vfs_starts_with(normalized, "/dev/")) {
        if (!mount || !rel_path || rel_path[0] == '\0') {
            return false; // Protect raw device nodes
        }
    }

    if (!mount || !mount->ops->rmdir) return false;
    return mount->ops->rmdir(mount->fs_private, rel_path);
}

bool vfs_delete(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized);

    // Protect root and virtual /dev directory itself
    if (normalized[0] == '/' && normalized[1] == '\0') return false;
    if (vfs_strcmp(normalized, "/dev") == 0) return false;

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    // If it's in /dev/, allow only if it's inside a mount beyond the device node
    if (vfs_starts_with(normalized, "/dev/")) {
        if (!mount || !rel_path || rel_path[0] == '\0') {
            return false; // Protect raw device nodes
        }
    }

    if (!mount || !mount->ops->unlink) return false;
    return mount->ops->unlink(mount->fs_private, rel_path);
}

bool vfs_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return false;

    char norm_old[VFS_MAX_PATH], norm_new[VFS_MAX_PATH];
    vfs_normalize_path(old_path, norm_old);
    vfs_normalize_path(new_path, norm_new);

    const char *rel_old = NULL, *rel_new = NULL;
    vfs_mount_t *mount_old = vfs_resolve_mount(norm_old, &rel_old);
    vfs_mount_t *mount_new = vfs_resolve_mount(norm_new, &rel_new);

    // Can only rename within the same mount
    if (!mount_old || mount_old != mount_new) return false;
    if (!mount_old->ops->rename) return false;

    if (!rel_old || rel_old[0] == '\0') return false;
    if (!rel_new || rel_new[0] == '\0') return false;

    return mount_old->ops->rename(mount_old->fs_private, rel_old, rel_new);
}

// ============================================================================
// Query Operations
// ============================================================================

bool vfs_exists(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized);

    // Root always exists
    if (normalized[0] == '/' && normalized[1] == '\0') return true;

    // Check if it's a prefix of any active mount point
    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_starts_with(mounts[i].path, normalized)) {
            spinlock_release_irqrestore(&vfs_lock, flags_vfs);
            return true;
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags_vfs);

    // /dev always exists as a virtual directory
    if (vfs_strcmp(normalized, "/dev") == 0) return true;

    // Check if it's a device in /dev
    if (vfs_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        if (disk_get_by_name(dev)) return true;
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount || !mount->ops->exists) return false;

    if (!rel_path || rel_path[0] == '\0') return true; // Mount point itself exists

    return mount->ops->exists(mount->fs_private, rel_path);
}

bool vfs_is_directory(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized);

    // Root is always a directory
    if (normalized[0] == '/' && normalized[1] == '\0') return true;

    // Check if it's a prefix of any active mount point (virtual directory)
    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_starts_with(mounts[i].path, normalized)) {
            // If it matches exactly a mount point, we still need to check if that FS is a dir
            if (vfs_strcmp(mounts[i].path, normalized) == 0) {
                // Exact mount point - it is a directory (mount root)
                spinlock_release_irqrestore(&vfs_lock, flags_vfs);
                return true;
            }
            // Prefix only - it is a virtual intermediate directory
            spinlock_release_irqrestore(&vfs_lock, flags_vfs);
            return true;
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags_vfs);

    // /dev is always a virtual directory
    if (vfs_strcmp(normalized, "/dev") == 0) return true;

    // Device check
    if (vfs_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        Disk *d = disk_get_by_name(dev);
        if (d) return d->is_partition ? true : false;
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount) return false;

    // If it's a mount point and we're at its root, it definitely exists as a dir
    if (!rel_path || rel_path[0] == '\0') return true;

    if (!mount->ops->is_dir) return false;
    return mount->ops->is_dir(mount->fs_private, rel_path);
}

int vfs_get_info(const char *path, vfs_dirent_t *info) {
    if (!path || !info) return -1;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized);

    // Root info
    if (normalized[0] == '/' && normalized[1] == '\0') {
        vfs_strcpy(info->name, "/");
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    // /dev virtual directory info
    if (vfs_strcmp(normalized, "/dev") == 0) {
        vfs_strcpy(info->name, "dev");
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    // Check if it's a prefix of any active mount point (virtual directory)
    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_starts_with(mounts[i].path, normalized)) {
            if (vfs_strcmp(mounts[i].path, normalized) != 0) {
                // Virtual intermediate directory
                // Get component name
                const char *p = normalized + vfs_strlen(normalized);
                while (p > normalized && *(p-1) != '/') p--;
                vfs_strcpy(info->name, p);
                info->size = 0;
                info->is_directory = 1;
                info->start_cluster = 0;
                info->write_date = 0;
                info->write_time = 0;
                spinlock_release_irqrestore(&vfs_lock, flags_vfs);
                return 0;
            }
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags_vfs);

    // Device check
    if (vfs_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        Disk *d = disk_get_by_name(dev);
        if (d) {
            vfs_strcpy(info->name, d->devname);
            info->size = d->total_sectors * 512;
            info->is_directory = d->is_partition ? 1 : 0;
            info->start_cluster = 0;
            info->write_date = 0;
            info->write_time = 0;
            return 0;
        }
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount || !mount->ops->get_info) return -1;

    if (!rel_path || rel_path[0] == '\0') {
        // Info about mount root
        vfs_strcpy(info->name, mount->device);
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    return mount->ops->get_info(mount->fs_private, rel_path, info);
}

// ============================================================================
// Mount Enumeration
// ============================================================================

int vfs_get_mount_count(void) {
    return mount_count;
}

vfs_mount_t* vfs_get_mount(int index) {
    if (index < 0 || index >= mount_count) return NULL;
    if (!mounts[index].active) return NULL;
    return &mounts[index];
}

// ============================================================================
// Auto-Mount (called when a new partition is discovered)
// ============================================================================

void vfs_automount_partition(const char *devname) {
    // Build mount point: /mnt/<devname>
    char mount_path[64] = "/mnt/";
    int i = 5;
    const char *d = devname;
    while (*d && i < 62) mount_path[i++] = *d++;
    mount_path[i] = 0;

    serial_write("[VFS] Auto-mount requested for ");
    serial_write(devname);
    serial_write(" at ");
    serial_write(mount_path);
    serial_write("\n");

    // The actual FAT32 volume creation and mount happens in disk_manager
    // after probing the partition. This function is called by disk_manager
    // after it has created the FAT32_Volume.
}
