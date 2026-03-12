// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "fat32.h"
#include "memory_manager.h"
#include "io.h"
#include "disk.h"
#include <stdbool.h>
#include <stddef.h>
#include "wm.h"


#define MAX_FILES 256
#define MAX_CLUSTERS 8192
#define MAX_OPEN_HANDLES 32

// In-memory FAT table
static uint32_t fat_table[MAX_CLUSTERS];
static uint8_t cluster_data[MAX_CLUSTERS][FAT32_CLUSTER_SIZE];

// File/Directory tracking
typedef struct {
    char full_path[FAT32_MAX_PATH];
    char filename[FAT32_MAX_FILENAME];
    uint32_t start_cluster;
    uint32_t size;
    uint32_t attributes;
    bool used;
    char parent_path[FAT32_MAX_PATH];
} FileEntry;

static FileEntry files[MAX_FILES];
static uint32_t next_cluster = 3;  // Start after reserved clusters 0, 1, 2
static FAT32_FileHandle open_handles[MAX_OPEN_HANDLES];
static char current_dir[FAT32_MAX_PATH] = "/";
static char current_drive = 'A';
static int desktop_file_limit = -1;

// === RealFS Definitions ===

typedef struct {
    Disk *disk;
    uint32_t fat_begin_lba;
    uint32_t cluster_begin_lba;
    uint32_t sectors_per_cluster;
    uint32_t root_cluster;
    uint32_t fat_size; // sectors
    uint32_t total_sectors;
    uint32_t partition_offset; // LBA offset of partition start
    bool mounted;
    uint32_t cached_fat_sector;
    uint8_t cached_fat_buf[512];
} FAT32_Volume;

static FAT32_Volume volumes[26]; // A-Z

// === Helper Functions (Shared) ===

// Serial debug output
static void fs_serial_char(char c) {
    while (!(inb(0x3F8 + 5) & 0x20));
    outb(0x3F8, c);
}
static void fs_serial_str(const char *s) { while (*s) fs_serial_char(*s++); }
static void fs_serial_num(uint32_t n) {
    if (n >= 10) fs_serial_num(n / 10);
    fs_serial_char('0' + (n % 10));
}

static size_t fs_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static void fs_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int fs_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void fs_strcat(char *dest, const char *src) {
    while (*dest) dest++;
    fs_strcpy(dest, src);
}


bool fs_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*prefix++ != *str++) return false;
    }
    return true;
}

// Extract filename from path
static void extract_filename(const char *path, char *filename) {
    int len = fs_strlen(path);
    int i = len - 1;
    while (i > 0 && path[i] == '/') i--;
    int start = i;
    while (start >= 0 && path[start] != '/') start--;
    start++;
    int j = 0;
    for (int k = start; k <= i; k++) {
        filename[j++] = path[k];
    }
    filename[j] = 0;
}

// Extract parent path
static void extract_parent_path(const char *path, char *parent) {
    int len = fs_strlen(path);
    int i = len - 1;
    while (i > 0 && path[i] == '/') i--;
    while (i > 0 && path[i] != '/') i--;
    if (i == 0) {
        parent[0] = '/';
        parent[1] = 0;
    } else {
        for (int j = 0; j < i; j++) {
            parent[j] = path[j];
        }
        parent[i] = 0;
    }
}

// Helper to parse drive from path
static char parse_drive_from_path(const char **path_ptr) {
    const char *path = *path_ptr;
    if (path[0] && path[1] == ':') {
        char drive = path[0];
        if (drive >= 'a' && drive <= 'z') drive -= 32; // toupper
        *path_ptr = path + 2;
        return drive;
    }
    return current_drive;
}

// Normalize path (remove .., ., etc)
void fat32_normalize_path(const char *path, char *normalized) {
    char temp[FAT32_MAX_PATH];
    int temp_len = 0;
    const char *p = path;
    char drive = parse_drive_from_path(&p);
    

    if (p[0] == '/') {
        fs_strcpy(temp, "/");
        temp_len = 1;
    } else {
        fs_strcpy(temp, current_dir);
        temp_len = fs_strlen(temp);
    }

    int i = 0;
    while (p[i]) {
        while (p[i] == '/') i++;
        if (!p[i]) break;
        char component[FAT32_MAX_FILENAME];
        int j = 0;
        while (p[i] && p[i] != '/' && j < (FAT32_MAX_FILENAME - 1)) {
            component[j++] = p[i++];
        }
        component[j] = 0;

        if (fs_strcmp(component, ".") == 0) {
            continue;
        } else if (fs_strcmp(component, "..") == 0) {
            if (temp_len > 1) {
                while (temp_len > 0 && temp[temp_len - 1] != '/') temp_len--;
                if (temp_len > 1) temp_len--;
                temp[temp_len] = 0;
            }
        } else {
            int comp_len = fs_strlen(component);
            if (temp_len + comp_len + 2 < FAT32_MAX_PATH) {
                if (temp[temp_len - 1] != '/') {
                    temp[temp_len++] = '/';
                    temp[temp_len] = 0;
                }
                fs_strcat(temp, component);
                temp_len = fs_strlen(temp);
            }
        }
    }
    if (temp_len > 1 && temp[temp_len - 1] == '/') temp[--temp_len] = 0;
    fs_strcpy(normalized, temp);
}

// === RAMFS Internal Functions ===

static FileEntry* ramfs_find_file(const char *path) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].full_path, normalized) == 0) {
            return &files[i];
        }
    }
    return NULL;
}

static FileEntry* ramfs_find_free_entry(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) return &files[i];
    }
    return NULL;
}

static FAT32_FileHandle* ramfs_find_free_handle(void) {
    for (int i = 0; i < MAX_OPEN_HANDLES; i++) {
        if (!open_handles[i].valid) return &open_handles[i];
    }
    return NULL;
}

static uint32_t ramfs_allocate_cluster(void) {
    if (next_cluster >= MAX_CLUSTERS) return 0;
    uint32_t cluster = next_cluster++;
    fat_table[cluster] = 0xFFFFFFFF;
    return cluster;
}

static bool check_desktop_limit(const char *normalized_path) {
    if (desktop_file_limit < 0) return true;
    if (fs_strlen(normalized_path) > 9 && 
        normalized_path[0] == '/' && 
        normalized_path[1] == 'D' && normalized_path[2] == 'e' && 
        normalized_path[3] == 's' && normalized_path[4] == 'k' && 
        normalized_path[5] == 't' && normalized_path[6] == 'o' && 
        normalized_path[7] == 'p' && normalized_path[8] == '/') {
        const char *p = normalized_path + 9;
        while (*p) {
            if (*p == '/') return true; 
            p++;
        }
        FAT32_FileInfo *info = (FAT32_FileInfo*)kmalloc(256 * sizeof(FAT32_FileInfo));
        if (!info) return true;
        int count = fat32_list_directory("/Desktop", info, 256);
        kfree(info);
        if (count >= desktop_file_limit) return false;
    }
    return true;
}

static FAT32_FileHandle* ramfs_open(const char *normalized_path, const char *mode) {
    FileEntry *entry = ramfs_find_file(normalized_path);
    
    if (mode[0] == 'r') {
        if (!entry || (entry->attributes & ATTR_DIRECTORY)) return NULL;
    } else if (mode[0] == 'w' || (mode[0] == 'a')) {
        if (!entry) {
            if (!check_desktop_limit(normalized_path)) return NULL;
            entry = ramfs_find_free_entry();
            if (!entry) return NULL;
            entry->used = true;
            fs_strcpy(entry->full_path, normalized_path);
            extract_filename(normalized_path, entry->filename);
            extract_parent_path(normalized_path, entry->parent_path);
            entry->start_cluster = ramfs_allocate_cluster();
            if (!entry->start_cluster) return NULL;
            entry->size = 0;
            entry->attributes = 0;
        }
        if (mode[0] == 'w') entry->size = 0;
    }
    
    FAT32_FileHandle *handle = ramfs_find_free_handle();
    if (!handle) return NULL;
    
    handle->valid = true;
    handle->drive = 'A';
    handle->cluster = entry->start_cluster;
    handle->start_cluster = entry->start_cluster;
    handle->position = 0;
    handle->size = entry->size;
    
    if (mode[0] == 'r') handle->mode = 0;
    else if (mode[0] == 'w') handle->mode = 1;
    else {
        handle->mode = 2;
        handle->position = entry->size;
        uint32_t current_cluster = handle->start_cluster;
        uint32_t pos = 0;
        while (pos + FAT32_CLUSTER_SIZE <= handle->position) {
             uint32_t next = fat_table[current_cluster];
             if (next >= 0xFFFFFFF8) break; 
             current_cluster = next;
             pos += FAT32_CLUSTER_SIZE;
        }
        handle->cluster = current_cluster;
    }
    return handle;
}

static int ramfs_read(FAT32_FileHandle *handle, void *buffer, int size) {
    int bytes_read = 0;
    uint8_t *buf = (uint8_t *)buffer;
    
    while (bytes_read < size && handle->position < handle->size) {
        uint32_t offset_in_cluster = handle->position % FAT32_CLUSTER_SIZE;
        int to_read = size - bytes_read;
        int available = handle->size - handle->position;
        if (to_read > available) to_read = available;
        if (to_read > (int)(FAT32_CLUSTER_SIZE - offset_in_cluster)) to_read = FAT32_CLUSTER_SIZE - offset_in_cluster;
        
        if (handle->cluster >= MAX_CLUSTERS) break;
        
        uint8_t *src = cluster_data[handle->cluster] + offset_in_cluster;
        for (int i = 0; i < to_read; i++) buf[bytes_read + i] = src[i];
        
        bytes_read += to_read;
        handle->position += to_read;
        if (handle->position % FAT32_CLUSTER_SIZE == 0 && handle->position < handle->size) {
            handle->cluster = fat_table[handle->cluster];
        }
    }
    return bytes_read;
}

static int ramfs_write(FAT32_FileHandle *handle, const void *buffer, int size) {
    int bytes_written = 0;
    const uint8_t *buf = (const uint8_t *)buffer;
    
    if (handle->position > 0 && (handle->position % FAT32_CLUSTER_SIZE) == 0) {
         uint32_t next = fat_table[handle->cluster];
         if (next >= 0xFFFFFFF8) {
             next = ramfs_allocate_cluster();
             if (!next) return 0;
             fat_table[handle->cluster] = next;
         }
         handle->cluster = next;
    }

    while (bytes_written < size) {
        uint32_t offset_in_cluster = handle->position % FAT32_CLUSTER_SIZE;
        int to_write = size - bytes_written;
        if (to_write > (int)(FAT32_CLUSTER_SIZE - offset_in_cluster)) to_write = FAT32_CLUSTER_SIZE - offset_in_cluster;
        
        if (handle->cluster >= MAX_CLUSTERS) break;
        
        uint8_t *dest = cluster_data[handle->cluster] + offset_in_cluster;
        for (int i = 0; i < to_write; i++) dest[i] = buf[bytes_written + i];
        
        bytes_written += to_write;
        handle->position += to_write;
        if (handle->position > handle->size) handle->size = handle->position;
        
        if (offset_in_cluster + to_write >= FAT32_CLUSTER_SIZE && bytes_written < size) {
            uint32_t next = ramfs_allocate_cluster();
            if (!next) break;
            fat_table[handle->cluster] = next;
            handle->cluster = next;
        }
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && files[i].start_cluster == handle->start_cluster) {
            files[i].size = handle->size;
            break;
        }
    }
    wm_notify_fs_change();
    return bytes_written;
}

// === RealFS Implementation ===

static bool realfs_mount(char drive) {
    int idx = drive - 'A';
    if (idx < 0 || idx >= 26) return false;
    
    if (volumes[idx].mounted) return true;
    
    Disk *disk = disk_get_by_letter(drive);
    if (!disk) return false;
    
    // Use partition LBA offset from disk (set during MBR parsing)
    uint32_t part_offset = disk->partition_lba_offset;
    
    uint8_t *sect0 = (uint8_t*)kmalloc(512);
    if (!sect0) return false;
    
    // Read BPB from partition start (sector 0 for raw, partition LBA for MBR)
    if (disk->read_sector(disk, part_offset, sect0) != 0) {
        kfree(sect0);
        return false;
    }
    
    FAT32_BootSector *bpb = (FAT32_BootSector*)sect0;
    
    if (bpb->boot_signature_value != 0xAA55) {
        kfree(sect0);
        return false;
    }
    
    volumes[idx].disk = disk;
    volumes[idx].partition_offset = part_offset;
    volumes[idx].fat_begin_lba = part_offset + bpb->reserved_sectors;
    volumes[idx].cluster_begin_lba = part_offset + bpb->reserved_sectors + (bpb->num_fats * bpb->sectors_per_fat_32);
    volumes[idx].sectors_per_cluster = bpb->sectors_per_cluster;
    volumes[idx].root_cluster = bpb->root_cluster;
    volumes[idx].fat_size = bpb->sectors_per_fat_32;
    volumes[idx].total_sectors = bpb->total_sectors_32;
    volumes[idx].mounted = true;
    volumes[idx].cached_fat_sector = 0xFFFFFFFF;
    
    fs_serial_str("[FAT32] mounted drive ");
    fs_serial_char(drive);
    fs_serial_str(": part_offset=");
    fs_serial_num(part_offset);
    fs_serial_str(" fat_lba=");
    fs_serial_num(volumes[idx].fat_begin_lba);
    fs_serial_str(" cluster_lba=");
    fs_serial_num(volumes[idx].cluster_begin_lba);
    fs_serial_str(" spc=");
    fs_serial_num(volumes[idx].sectors_per_cluster);
    fs_serial_str(" root_cl=");
    fs_serial_num(volumes[idx].root_cluster);
    fs_serial_str("\n");
    
    kfree(sect0);
    return true;
}

static uint32_t realfs_next_cluster(FAT32_Volume *vol, uint32_t cluster);
static void realfs_update_dir_entry_size(FAT32_Volume *vol, FAT32_FileHandle *handle) {
    if (handle->dir_sector != 0 && handle->dir_offset != 0xFFFFFFFF && handle->dir_offset < 512) {
        uint8_t *dir_buf = (uint8_t*)kmalloc(512);
        if (dir_buf && vol->disk->read_sector(vol->disk, handle->dir_sector, dir_buf) == 0) {
            FAT32_DirEntry *entry = (FAT32_DirEntry*)(dir_buf + handle->dir_offset);
            // Update start cluster if it exists
            if (handle->start_cluster != 0) {
                entry->start_cluster_high = (handle->start_cluster >> 16);
                entry->start_cluster_low = (handle->start_cluster & 0xFFFF);
            }
            // Always update file size
            entry->file_size = handle->size;
            // Write back
            vol->disk->write_sector(vol->disk, handle->dir_sector, dir_buf);
        }
        if (dir_buf) kfree(dir_buf);
    }
}

static uint32_t realfs_next_cluster(FAT32_Volume *vol, uint32_t cluster) {
    uint32_t fat_sector = vol->fat_begin_lba + (cluster * 4) / 512;
    uint32_t fat_offset = (cluster * 4) % 512;
    
    if (vol->cached_fat_sector != fat_sector) {
        if (vol->disk->read_sector(vol->disk, fat_sector, vol->cached_fat_buf) != 0) {
            return 0xFFFFFFFF;
        }
        vol->cached_fat_sector = fat_sector;
    }
    
    uint32_t next = *(uint32_t*)&vol->cached_fat_buf[fat_offset];
    next &= 0x0FFFFFFF; // Mask top 4 bits
    
    return next;
}

static int realfs_read_cluster(FAT32_Volume *vol, uint32_t cluster, uint8_t *buffer) {
    uint32_t lba = vol->cluster_begin_lba + (cluster - 2) * vol->sectors_per_cluster;
    for (uint32_t i = 0; i < vol->sectors_per_cluster; i++) {
        if (vol->disk->read_sector(vol->disk, lba + i, buffer + (i * 512)) != 0) return -1;
    }
    return 0;
}

static void to_dos_filename(const char *filename, char *out) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int len = fs_strlen(filename);
    int dot = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (filename[i] == '.') { dot = i; break; }
    }
    int name_len = (dot == -1) ? len : dot;
    if (name_len > 8) name_len = 8;
    for (int i = 0; i < name_len; i++) {
        char c = filename[i];
        // Preserve case - don't convert to uppercase
        out[i] = c;
    }
    if (dot != -1) {
        int ext_len = len - dot - 1;
        if (ext_len > 3) ext_len = 3;
        for (int i = 0; i < ext_len; i++) {
            char c = filename[dot + 1 + i];
            // Preserve case - don't convert to uppercase
            out[8 + i] = c;
        }
    }
}

static FAT32_FileHandle* realfs_open(char drive, const char *path, const char *mode) {
    int vol_idx = drive - 'A';
    if (!volumes[vol_idx].mounted) {
        if (!realfs_mount(drive)) return NULL;
    }
    FAT32_Volume *vol = &volumes[vol_idx];
    
    // Parse path to find start cluster
    uint32_t current_cluster = vol->root_cluster;
    
    // Skip leading slash
    const char *p = path;
    if (*p == '/') p++;
    

    
    if (*p == 0) {
        // Root dir
        if (mode[0] == 'w') return NULL; // Cannot write to root as file
        FAT32_FileHandle *fh = ramfs_find_free_handle(); // Reuse handle pool
        if (fh) {
            fh->valid = true;
            fh->drive = drive;
            fh->start_cluster = vol->root_cluster;
            fh->cluster = vol->root_cluster;
            fh->position = 0;
            fh->size = 0; // Unknown for root
            fh->mode = 0;
            return fh;
        }
        return NULL;
    }
    
    char component[256];
    bool found = false;
    uint32_t file_size = 0;
    
    uint32_t entry_sector = 0;
    uint32_t entry_offset = 0;
    
    while (*p) {
        // Extract component
        int i = 0;
        while (*p && *p != '/') {
            component[i++] = *p++;
        }
        component[i] = 0;
        if (*p == '/') p++; // Skip separator
        
        // Search in current_cluster
        found = false;
        uint32_t search_cluster = current_cluster;
        uint8_t *cluster_buf = (uint8_t*)kmalloc(vol->sectors_per_cluster * 512);
        if (!cluster_buf) return NULL;
        
        while (search_cluster < 0x0FFFFFF8) {
            if (realfs_read_cluster(vol, search_cluster, cluster_buf) != 0) break;
            
            FAT32_DirEntry *entry = (FAT32_DirEntry*)cluster_buf;
            int entries_per_cluster = (vol->sectors_per_cluster * 512) / 32;
            
            for (int e = 0; e < entries_per_cluster; e++) {
                if (entry[e].filename[0] == 0) break; // End of dir
                if (entry[e].filename[0] == 0xE5) continue; // Deleted
                if (entry[e].attributes == 0x0F) continue; // LFN entry
                if (entry[e].attributes & ATTR_VOLUME_ID) continue; // Volume label
                
                // Compare name (simplistic 8.3 matching)
                char name[13];
                int n = 0;
                for (int k = 0; k < 8 && entry[e].filename[k] != ' '; k++) name[n++] = entry[e].filename[k];
                if (entry[e].extension[0] != ' ') {
                    name[n++] = '.';
                    for (int k = 0; k < 3 && entry[e].extension[k] != ' '; k++) name[n++] = entry[e].extension[k];
                }
                name[n] = 0;
                
                // Case insensitive compare
                bool match = true;
                for (int c = 0; c < n+1; c++) {
                    char c1 = name[c];
                    char c2 = component[c];
                    if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
                    if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
                    if (c1 != c2) { match = false; break; }
                }
                
                if (match) {
                    uint32_t cluster = (entry[e].start_cluster_high << 16) | entry[e].start_cluster_low;
                    
                    uint32_t lba = vol->cluster_begin_lba + (search_cluster - 2) * vol->sectors_per_cluster;
                    int sect_in_cluster = (e * 32) / 512;
                    entry_sector = lba + sect_in_cluster;
                    entry_offset = (e * 32) % 512;

                    if (*p == 0) {
                        // Found target
                        current_cluster = cluster;
                        file_size = entry[e].file_size;
                        found = true;
                    } else {
                        // It must be a directory
                        if (entry[e].attributes & ATTR_DIRECTORY) {
                            current_cluster = cluster;
                            found = true;
                        }
                    }
                    break;
                }
            }
            if (found) break;
            search_cluster = realfs_next_cluster(vol, search_cluster);
        }
        
        if (!found) {
            if ((mode[0] == 'w' || mode[0] == 'a') && *p == 0) {
                 // Create file logic
                 char dos_name[11];
                 to_dos_filename(component, dos_name);
                 
                 // Find free entry in current_cluster (which is the directory)
                 search_cluster = current_cluster;
                 bool found_free = false;
                 uint32_t free_sector = 0;
                 uint32_t free_offset = 0;
                 
                 while (search_cluster < 0x0FFFFFF8 && !found_free) {
                     if (realfs_read_cluster(vol, search_cluster, cluster_buf) != 0) break;
                     FAT32_DirEntry *entries = (FAT32_DirEntry*)cluster_buf;
                     int count = (vol->sectors_per_cluster * 512) / 32;
                     
                     for (int e = 0; e < count; e++) {
                          if (entries[e].filename[0] == 0 || entries[e].filename[0] == 0xE5) {
                              uint32_t lba = vol->cluster_begin_lba + (search_cluster - 2) * vol->sectors_per_cluster;
                              int sect_in_cluster = (e * 32) / 512;
                              free_sector = lba + sect_in_cluster;
                              free_offset = (e * 32) % 512;
                              found_free = true;
                              break;
                          }
                     }
                     if (!found_free) search_cluster = realfs_next_cluster(vol, search_cluster);
                 }
                 
                 if (found_free) {
                     uint8_t *sect_buf = (uint8_t*)kmalloc(512);
                     if (sect_buf) {
                         vol->disk->read_sector(vol->disk, free_sector, sect_buf);
                         FAT32_DirEntry *d = (FAT32_DirEntry*)(sect_buf + free_offset);
                         
                         for(int k=0; k<8; k++) d->filename[k] = dos_name[k];
                         for(int k=0; k<3; k++) d->extension[k] = dos_name[8+k];
                         d->attributes = ATTR_ARCHIVE;
                         d->start_cluster_high = 0;
                         d->start_cluster_low = 0;
                         d->file_size = 0;
                         
                         // Write to disk
                         if (vol->disk->write_sector(vol->disk, free_sector, sect_buf) != 0) {
                             kfree(sect_buf);
                             kfree(cluster_buf);
                             return NULL;
                         }
                         kfree(sect_buf);
                         
                         FAT32_FileHandle *fh = ramfs_find_free_handle();
                         if (fh) {
                             fh->valid = true;
                             fh->drive = drive;
                             fh->start_cluster = 0;
                             fh->cluster = 0;
                             fh->position = 0;
                             fh->size = 0;
                             fh->mode = (mode[0] == 'a' ? 2 : 1);
                             fh->dir_sector = free_sector;
                             fh->dir_offset = free_offset;
                             kfree(cluster_buf);
                             return fh;
                         }
                     }
                 }
                 kfree(cluster_buf);
                 return NULL; 
            }
            kfree(cluster_buf);
            return NULL;
        }
        kfree(cluster_buf);
    }
    
    // Found file/dir
    FAT32_FileHandle *fh = ramfs_find_free_handle();
    if (fh) {
        fh->valid = true;
        fh->drive = drive;
        fh->start_cluster = current_cluster;
        fh->cluster = current_cluster;
        fh->position = 0;
        fh->size = file_size;
        fh->mode = (mode[0] == 'w' ? 1 : (mode[0] == 'a' ? 2 : 0));
        fh->dir_sector = entry_sector;
        fh->dir_offset = entry_offset;
        
        if (mode[0] == 'a') {
            fh->position = fh->size;
            // Seek to EOF cluster
            uint32_t cluster_size = vol->sectors_per_cluster * 512;
            uint32_t pos = 0;
            while (pos + cluster_size <= fh->position) {
                uint32_t next = realfs_next_cluster(vol, fh->cluster);
                if (next >= 0x0FFFFFF8) break;
                fh->cluster = next;
                pos += cluster_size;
            }
        }
        return fh;
    }
    
    return NULL;
}

static int realfs_read(FAT32_FileHandle *handle, void *buffer, int size) {
    int vol_idx = handle->drive - 'A';
    FAT32_Volume *vol = &volumes[vol_idx];
    
    uint8_t *cluster_buf = (uint8_t*)kmalloc(vol->sectors_per_cluster * 512);
    if (!cluster_buf) return 0;
    
    int bytes_read = 0;
    uint8_t *out_buf = (uint8_t*)buffer;
    uint32_t cluster_size = vol->sectors_per_cluster * 512;
    
    while (bytes_read < size && handle->position < handle->size) {
        if (realfs_read_cluster(vol, handle->cluster, cluster_buf) != 0) break;
        
        uint32_t offset = handle->position % cluster_size;
        int to_copy = size - bytes_read;
        int available = cluster_size - offset;
        if ((int)(handle->size - handle->position) < available) available = handle->size - handle->position;
        if (to_copy > available) to_copy = available;
        
        for (int i = 0; i < to_copy; i++) {
            out_buf[bytes_read + i] = cluster_buf[offset + i];
        }
        
        bytes_read += to_copy;
        handle->position += to_copy;
        
        if (handle->position % cluster_size == 0 && handle->position < handle->size) {
            handle->cluster = realfs_next_cluster(vol, handle->cluster);
            if (handle->cluster >= 0x0FFFFFF8) break;
        }
    }
    
    kfree(cluster_buf);
    return bytes_read;
}

static int realfs_write_cluster(FAT32_Volume *vol, uint32_t cluster, const uint8_t *buffer) {
    uint32_t lba = vol->cluster_begin_lba + (cluster - 2) * vol->sectors_per_cluster;
    for (uint32_t i = 0; i < vol->sectors_per_cluster; i++) {
        if (vol->disk->write_sector(vol->disk, lba + i, buffer + (i * 512)) != 0) return -1;
    }
    return 0;
}

static uint32_t realfs_allocate_cluster(FAT32_Volume *vol) {
    uint32_t current = 2;
    uint32_t fat_entries = (vol->fat_size * 512) / 4;
    
    uint8_t *fat_buf = (uint8_t*)kmalloc(512);
    if (!fat_buf) return 0;
    
    uint32_t cached_sector = 0xFFFFFFFF;
    
    while (current < fat_entries) {
        uint32_t sector = vol->fat_begin_lba + (current * 4) / 512;
        uint32_t offset = (current * 4) % 512;
        
        if (sector != cached_sector) {
            vol->disk->read_sector(vol->disk, sector, fat_buf);
            cached_sector = sector;
        }
        
        uint32_t val = *(uint32_t*)&fat_buf[offset];
        if ((val & 0x0FFFFFFF) == 0) {
            *(uint32_t*)&fat_buf[offset] = 0x0FFFFFFF; // EOC
            vol->disk->write_sector(vol->disk, sector, fat_buf);
            if (vol->cached_fat_sector == sector) {
                vol->cached_fat_sector = 0xFFFFFFFF;
            }
            kfree(fat_buf);
            return current;
        }
        current++;
    }
    kfree(fat_buf);
    return 0; // Full
}

static int realfs_write(FAT32_FileHandle *handle, const void *buffer, int size) {
    int vol_idx = handle->drive - 'A';
    FAT32_Volume *vol = &volumes[vol_idx];
    
    if (handle->start_cluster == 0) {
        uint32_t new_cluster = realfs_allocate_cluster(vol);
        if (new_cluster == 0) return 0;
        handle->start_cluster = new_cluster;
        handle->cluster = new_cluster;
        
        // Mark new cluster as EOF in FAT, preserve high bits
        uint32_t fat_sector = vol->fat_begin_lba + (new_cluster * 4) / 512;
        uint32_t fat_offset = (new_cluster * 4) % 512;
        uint8_t *fat_buf = (uint8_t*)kmalloc(512);
        if (fat_buf && vol->disk->read_sector(vol->disk, fat_sector, fat_buf) == 0) {
             uint32_t old_val = *(uint32_t*)&fat_buf[fat_offset];
             *(uint32_t*)&fat_buf[fat_offset] = (old_val & 0xF0000000) | 0x0FFFFFF8;  // EOF
             vol->disk->write_sector(vol->disk, fat_sector, fat_buf);
             if (vol->cached_fat_sector == fat_sector) vol->cached_fat_sector = 0xFFFFFFFF;
        }
        if (fat_buf) kfree(fat_buf);
        
        // Initialize the new cluster with zeros to avoid garbage
        uint32_t cluster_size_bytes = vol->sectors_per_cluster * 512;
        uint8_t *cbuf = (uint8_t*)kmalloc(cluster_size_bytes);
        if (cbuf) {
            for(uint32_t i=0; i<cluster_size_bytes; i++) cbuf[i] = 0;
            realfs_write_cluster(vol, new_cluster, cbuf);
            kfree(cbuf);
        }
        
        // Update directory entry immediately with the allocated cluster and initial size
        if (handle->dir_sector != 0 && handle->dir_offset != 0xFFFFFFFF && handle->dir_offset < 512) {
            uint8_t *dir_buf = (uint8_t*)kmalloc(512);
            if (dir_buf && vol->disk->read_sector(vol->disk, handle->dir_sector, dir_buf) == 0) {
                FAT32_DirEntry *entry = (FAT32_DirEntry*)(dir_buf + handle->dir_offset);
                entry->start_cluster_high = (new_cluster >> 16);
                entry->start_cluster_low = (new_cluster & 0xFFFF);
                entry->file_size = 0;  // Start with size 0
                vol->disk->write_sector(vol->disk, handle->dir_sector, dir_buf);
            }
            if (dir_buf) kfree(dir_buf);
        }
    }
    
    uint8_t *cluster_buf = (uint8_t*)kmalloc(vol->sectors_per_cluster * 512);
    if (!cluster_buf) return 0;
    
    int bytes_written = 0;
    const uint8_t *src_buf = (const uint8_t*)buffer;
    uint32_t cluster_size = vol->sectors_per_cluster * 512;
    
    while (bytes_written < size) {
        if (realfs_read_cluster(vol, handle->cluster, cluster_buf) != 0) break;
        
        uint32_t offset = handle->position % cluster_size;
        int to_copy = size - bytes_written;
        int available = cluster_size - offset; 
        
        if (to_copy > available) to_copy = available;
        
        for (int i = 0; i < to_copy; i++) {
            cluster_buf[offset + i] = src_buf[bytes_written + i];
        }
        
        if (realfs_write_cluster(vol, handle->cluster, cluster_buf) != 0) break;
        
        bytes_written += to_copy;
        handle->position += to_copy;
        
        if (handle->position > handle->size) {
            handle->size = handle->position;
        }
        
        // Update directory entry after every write to ensure persistence
        if (handle->size > 0) {
            realfs_update_dir_entry_size(vol, handle);
        }
        
        if (handle->position % cluster_size == 0 && bytes_written < size) {
            uint32_t next = realfs_next_cluster(vol, handle->cluster);
            // If next is 0 (unallocated) or EOC, we must allocate a new cluster
            if (next < 2 || next >= 0x0FFFFFF8) {
                uint32_t new_cluster = realfs_allocate_cluster(vol);
                if (new_cluster == 0) break;
                
                // Link current cluster to new cluster in FAT, preserve high bits
                uint32_t fat_sector = vol->fat_begin_lba + (handle->cluster * 4) / 512;
                uint32_t fat_offset = (handle->cluster * 4) % 512;
                
                uint8_t *fat_buf = (uint8_t*)kmalloc(512);
                if (fat_buf && vol->disk->read_sector(vol->disk, fat_sector, fat_buf) == 0) {
                     uint32_t old_val = *(uint32_t*)&fat_buf[fat_offset];
                     *(uint32_t*)&fat_buf[fat_offset] = (old_val & 0xF0000000) | (new_cluster & 0x0FFFFFFF);
                     vol->disk->write_sector(vol->disk, fat_sector, fat_buf);
                     if (vol->cached_fat_sector == fat_sector) vol->cached_fat_sector = 0xFFFFFFFF;
                }
                if (fat_buf) kfree(fat_buf);
                
                // Mark new cluster as EOF in FAT, preserve high bits
                fat_sector = vol->fat_begin_lba + (new_cluster * 4) / 512;
                fat_offset = (new_cluster * 4) % 512;
                fat_buf = (uint8_t*)kmalloc(512);
                if (fat_buf && vol->disk->read_sector(vol->disk, fat_sector, fat_buf) == 0) {
                     uint32_t old_val = *(uint32_t*)&fat_buf[fat_offset];
                     *(uint32_t*)&fat_buf[fat_offset] = (old_val & 0xF0000000) | 0x0FFFFFF8;  // EOF
                     vol->disk->write_sector(vol->disk, fat_sector, fat_buf);
                     if (vol->cached_fat_sector == fat_sector) vol->cached_fat_sector = 0xFFFFFFFF;
                }
                if (fat_buf) kfree(fat_buf);
                
                // Init new cluster
                uint8_t *cbuf = (uint8_t*)kmalloc(cluster_size);
                if (cbuf) {
                    for(uint32_t k=0; k<cluster_size; k++) cbuf[k] = 0;
                    realfs_write_cluster(vol, new_cluster, cbuf);
                    kfree(cbuf);
                }
                
                next = new_cluster;
            }
            handle->cluster = next;
        }
    }
    
    // Final update to directory entry with complete file size before returning
    realfs_update_dir_entry_size(vol, handle);
    
    kfree(cluster_buf);
    return bytes_written;
}

static bool realfs_delete(char drive, const char *path) {
    int vol_idx = drive - 'A';
    if (!volumes[vol_idx].mounted) {
        if (!realfs_mount(drive)) return false;
    }
    FAT32_Volume *vol = &volumes[vol_idx];
    
    // Parse path to find start cluster and directory entry location
    uint32_t current_cluster = vol->root_cluster;
    
    const char *p = path;
    if (*p == '/') p++;
    
    if (*p == 0) {
        return false; // Cannot delete root
    }
    
    char component[256];
    uint32_t file_start_cluster = 0;
    uint32_t entry_sector = 0;
    uint32_t entry_offset = 0;
    bool is_directory = false;
    
    uint8_t *cluster_buf = (uint8_t*)kmalloc(vol->sectors_per_cluster * 512);
    if (!cluster_buf) return false;
    
    while (*p) {
        // Extract component
        int i = 0;
        while (*p && *p != '/') {
            component[i++] = *p++;
        }
        component[i] = 0;
        if (*p == '/') p++; // Skip separator
        
        // Search in current_cluster
        bool found = false;
        uint32_t search_cluster = current_cluster;
        
        while (search_cluster < 0x0FFFFFF8) {
            if (realfs_read_cluster(vol, search_cluster, cluster_buf) != 0) break;
            
            FAT32_DirEntry *entry = (FAT32_DirEntry*)cluster_buf;
            int entries_per_cluster = (vol->sectors_per_cluster * 512) / 32;
            
            for (int e = 0; e < entries_per_cluster; e++) {
                if (entry[e].filename[0] == 0) break;
                if (entry[e].filename[0] == 0xE5) continue;
                if (entry[e].attributes == 0x0F) continue; // Skip LFN entries
                if (entry[e].attributes & ATTR_VOLUME_ID) continue; // Skip volume label
                
                // Format name and compare
                char name[13];
                int n = 0;
                for (int k = 0; k < 8 && entry[e].filename[k] != ' '; k++) name[n++] = entry[e].filename[k];
                if (entry[e].extension[0] != ' ') {
                    name[n++] = '.';
                    for (int k = 0; k < 3 && entry[e].extension[k] != ' '; k++) name[n++] = entry[e].extension[k];
                }
                name[n] = 0;
                
                // Case insensitive compare
                bool match = true;
                for (int c = 0; c < n+1; c++) {
                    char c1 = name[c];
                    char c2 = component[c];
                    if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
                    if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
                    if (c1 != c2) { match = false; break; }
                }
                
                if (match) {
                    file_start_cluster = (entry[e].start_cluster_high << 16) | entry[e].start_cluster_low;
                    is_directory = (entry[e].attributes & ATTR_DIRECTORY) != 0;
                    
                    uint32_t lba = vol->cluster_begin_lba + (search_cluster - 2) * vol->sectors_per_cluster;
                    int sect_in_cluster = (e * 32) / 512;
                    entry_sector = lba + sect_in_cluster;
                    entry_offset = (e * 32) % 512;
                    
                    if (*p == 0) {
                        // Found target file/directory to delete
                        found = true;
                    } else {
                        // It must be a directory to continue traversing
                        if (is_directory) {
                            current_cluster = file_start_cluster;
                            found = true;
                        }
                    }
                    break;
                }
            }
            if (found) break;
            search_cluster = realfs_next_cluster(vol, search_cluster);
        }
        
        if (!found) {
            kfree(cluster_buf);
            return false; // Path not found
        }
        
        if (*p == 0) break; // End of path
    }
    
    
    // 1. Mark directory entry as deleted
    uint8_t *entry_buf = (uint8_t*)kmalloc(512);
    if (!entry_buf) {
        kfree(cluster_buf);
        return false;
    }
    
    if (vol->disk->read_sector(vol->disk, entry_sector, entry_buf) != 0) {
        kfree(entry_buf);
        kfree(cluster_buf);
        return false;
    }
    
    // Mark as deleted
    entry_buf[entry_offset] = 0xE5;
    
    if (vol->disk->write_sector(vol->disk, entry_sector, entry_buf) != 0) {
        kfree(entry_buf);
        kfree(cluster_buf);
        return false;
    }
    
    // 2. Free all clusters used by the file
    if (file_start_cluster != 0 && file_start_cluster < 0x0FFFFFF8) {
        uint32_t current = file_start_cluster;
        
        while (current < 0x0FFFFFF8) {
            uint32_t next = realfs_next_cluster(vol, current);
            
            // Mark this cluster as free in FAT
            uint32_t fat_sector = vol->fat_begin_lba + (current * 4) / 512;
            uint32_t fat_offset = (current * 4) % 512;
            
            uint8_t *fat_buf = (uint8_t*)kmalloc(512);
            if (!fat_buf) break;
            
            if (vol->disk->read_sector(vol->disk, fat_sector, fat_buf) == 0) {
                *(uint32_t*)&fat_buf[fat_offset] = 0; // Free
                vol->disk->write_sector(vol->disk, fat_sector, fat_buf);
                if (vol->cached_fat_sector == fat_sector) vol->cached_fat_sector = 0xFFFFFFFF;
            }
            kfree(fat_buf);
            
            current = next;
        }
    }
    
    kfree(entry_buf);
    kfree(cluster_buf);
    return true;
}

static int realfs_list_directory(char drive, const char *path, FAT32_FileInfo *entries, int max_entries) {
    int vol_idx = drive - 'A';
    if (!volumes[vol_idx].mounted) {
        if (!realfs_mount(drive)) return 0;
    }
    FAT32_Volume *vol = &volumes[vol_idx];
    

    FAT32_FileHandle *dir_handle = realfs_open(drive, path, "r");
    if (!dir_handle) return 0;
    
    uint32_t current_cluster = dir_handle->start_cluster;
    fat32_close(dir_handle); // Return to pool - this invalidates the handle
    
    int count = 0;
    uint8_t *cluster_buf = (uint8_t*)kmalloc(vol->sectors_per_cluster * 512);
    if (!cluster_buf) return 0;
    
    while (current_cluster < 0x0FFFFFF8 && count < max_entries) {
        if (realfs_read_cluster(vol, current_cluster, cluster_buf) != 0) break;
        
        FAT32_DirEntry *entry = (FAT32_DirEntry*)cluster_buf;
        int entries_per_cluster = (vol->sectors_per_cluster * 512) / 32;
        
        for (int e = 0; e < entries_per_cluster && count < max_entries; e++) {
            if (entry[e].filename[0] == 0) break;
            if (entry[e].filename[0] == 0xE5) continue;
            if (entry[e].attributes == 0x0F) continue; // Skip LFN entries
            if (entry[e].attributes & ATTR_VOLUME_ID) continue; // Skip volume label
            
            // Format name
            char name[13];
            int n = 0;
            for (int k = 0; k < 8 && entry[e].filename[k] != ' '; k++) name[n++] = entry[e].filename[k];
            if (entry[e].extension[0] != ' ') {
                name[n++] = '.';
                for (int k = 0; k < 3 && entry[e].extension[k] != ' '; k++) name[n++] = entry[e].extension[k];
            }
            name[n] = 0;
            
            // Skip . and ..
            if (fs_strcmp(name, ".") == 0 || fs_strcmp(name, "..") == 0) continue;
            
            fs_strcpy(entries[count].name, name);
            entries[count].size = entry[e].file_size;
            entries[count].is_directory = (entry[e].attributes & ATTR_DIRECTORY);
            entries[count].start_cluster = (entry[e].start_cluster_high << 16) | entry[e].start_cluster_low;
            count++;
        }
        
        current_cluster = realfs_next_cluster(vol, current_cluster);
    }
    
    kfree(cluster_buf);
    return count;
}


// === Public API (Dispatch) ===

void fat32_init(void) {
    // Explicitly zero out all structures for RAMFS safety
    for (int i = 0; i < MAX_FILES; i++) {
        files[i].used = false;
        files[i].full_path[0] = 0;
        files[i].filename[0] = 0;
        files[i].parent_path[0] = 0;
        files[i].size = 0;
        files[i].attributes = 0;
        files[i].start_cluster = 0;
    }

    // Initialize FAT table for RAMFS
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        fat_table[i] = 0;
    }
    fat_table[0] = 0xFFFFFFF8;
    fat_table[1] = 0xFFFFFFFF;

    // Zero out cluster data
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        for (int j = 0; j < FAT32_CLUSTER_SIZE; j++) {
            cluster_data[i][j] = 0;
        }
    }
    
    // Create root directory entry for RAMFS
    FileEntry *root = ramfs_find_free_entry();
    if (root) {
        root->used = true;
        root->filename[0] = 0;
        fs_strcpy(root->full_path, "/");
        root->start_cluster = 2;
        root->size = 0;
        root->attributes = ATTR_DIRECTORY;
        fat_table[2] = 0xFFFFFFFF;
    }
    
    next_cluster = 3;
    current_dir[0] = '/';
    current_dir[1] = 0;
    current_drive = 'A';
    
    // Reset Volumes
    for(int i=0; i<26; i++) volumes[i].mounted = false;
}

void fat32_set_desktop_limit(int limit) {
    desktop_file_limit = limit;
}

bool fat32_change_drive(char drive) {
    if (drive >= 'a' && drive <= 'z') drive -= 32;
    Disk *d = disk_get_by_letter(drive);
    if (d) {
        current_drive = drive;
        current_dir[0] = '/';
        current_dir[1] = 0;
        return true;
    }
    return false;
}

char fat32_get_current_drive(void) {
    return current_drive;
}

FAT32_FileHandle* fat32_open(const char *path, const char *mode) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));

    const char *p = path;
    char drive = parse_drive_from_path(&p);
    
    FAT32_FileHandle *handle = NULL;
    if (drive == 'A') {
        char normalized[FAT32_MAX_PATH];
        fat32_normalize_path(p, normalized);
        handle = ramfs_open(normalized, mode);
    } else {
        // Real Drive
        handle = realfs_open(drive, p, mode);
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return handle;
}

void fat32_close(FAT32_FileHandle *handle) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    if (handle && handle->valid) {
        if (handle->drive != 'A' && handle->mode != 0) {  // Both read and write modes for real drives
            Disk *d = disk_get_by_letter(handle->drive);
            if (d && handle->dir_sector != 0) {
                 uint8_t *buf = (uint8_t*)kmalloc(512);
                 if (buf) {
                     if (d->read_sector(d, handle->dir_sector, buf) == 0) {
                         FAT32_DirEntry *entry = (FAT32_DirEntry*)(buf + handle->dir_offset);
                         // Always update file size
                         entry->file_size = handle->size;
                         // Update start cluster if it exists
                         if (handle->start_cluster != 0) {
                             entry->start_cluster_high = (handle->start_cluster >> 16);
                             entry->start_cluster_low = (handle->start_cluster & 0xFFFF);
                         }
                         if (d->write_sector(d, handle->dir_sector, buf) != 0) {
                         }
                     }
                     kfree(buf);
                 }
            }
        }
        handle->valid = false;
    }
    asm volatile("push %0; popfq" : : "r"(rflags));
}

int fat32_read(FAT32_FileHandle *handle, void *buffer, int size) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    if (!handle || !handle->valid || handle->mode != 0) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return -1;
    }
    
    int ret = 0;
    if (handle->drive == 'A') {
        ret = ramfs_read(handle, buffer, size);
    } else {
        ret = realfs_read(handle, buffer, size);
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return ret;
}

int fat32_write(FAT32_FileHandle *handle, const void *buffer, int size) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    if (!handle || !handle->valid || (handle->mode != 1 && handle->mode != 2)) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return -1;
    }
    
    int ret = 0;
    if (handle->drive == 'A') {
        ret = ramfs_write(handle, buffer, size);
    } else {
        ret = realfs_write(handle, buffer, size);
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return ret;
}

int fat32_seek(FAT32_FileHandle *handle, int offset, int whence) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    if (!handle || !handle->valid) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return -1;
    }
    
    uint32_t new_position = handle->position;
    if (whence == 0) new_position = offset;
    else if (whence == 1) new_position += offset;
    else if (whence == 2) new_position = handle->size + offset;
    
    if (new_position > handle->size) new_position = handle->size;
    
    handle->position = new_position;
    
    // Both RealFS and RAMFS must accurately re-walk their cluster chains
    if (handle->drive == 'A') {
        handle->cluster = handle->start_cluster;
        uint32_t pos = 0;
        while (pos + FAT32_CLUSTER_SIZE <= handle->position) {
             uint32_t next = fat_table[handle->cluster];
             if (next >= 0xFFFFFFF8) break;
             handle->cluster = next;
             pos += FAT32_CLUSTER_SIZE;
        }
    } else {
        // Re-walk to find current cluster
        int vol_idx = handle->drive - 'A';
        FAT32_Volume *vol = &volumes[vol_idx];
        uint32_t cluster_size = vol->sectors_per_cluster * 512;
        
        handle->cluster = handle->start_cluster;
        uint32_t pos = 0;
        while (pos + cluster_size <= handle->position) {
             uint32_t next = realfs_next_cluster(vol, handle->cluster);
             if (next >= 0x0FFFFFF8) break;
             handle->cluster = next;
             pos += cluster_size;
        }
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return new_position;
}

static bool realfs_mkdir(char drive, const char *path) {
    int vol_idx = drive - 'A';
    if (!volumes[vol_idx].mounted) {
        if (!realfs_mount(drive)) return false;
    }
    FAT32_Volume *vol = &volumes[vol_idx];

    // Find parent directory and name of new directory
    char parent_path[FAT32_MAX_PATH];
    char dirname[FAT32_MAX_FILENAME];
    extract_parent_path(path, parent_path);
    extract_filename(path, dirname);

    // Open parent directory
    FAT32_FileHandle *parent_fh = realfs_open(drive, parent_path, "r");
    if (!parent_fh) return false;
    uint32_t parent_cluster = parent_fh->start_cluster;
    fat32_close(parent_fh);

    // Check if it already exists
    FAT32_FileHandle *check_fh = realfs_open(drive, path, "r");
    if (check_fh) {
        fat32_close(check_fh);
        return false;
    }

    // Allocate cluster for new directory
    uint32_t new_cluster = realfs_allocate_cluster(vol);
    if (new_cluster == 0) return false;

    // Initialize new directory cluster with . and .. entries
    uint32_t cluster_size = vol->sectors_per_cluster * 512;
    uint8_t *cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) return false;
    for (uint32_t i = 0; i < cluster_size; i++) cluster_buf[i] = 0;

    FAT32_DirEntry *dot = (FAT32_DirEntry*)cluster_buf;
    FAT32_DirEntry *dotdot = (FAT32_DirEntry*)(cluster_buf + 32);

    // . entry
    for (int i = 0; i < 8; i++) dot->filename[i] = ' ';
    for (int i = 0; i < 3; i++) dot->extension[i] = ' ';
    dot->filename[0] = '.';
    dot->attributes = ATTR_DIRECTORY;
    dot->start_cluster_high = (new_cluster >> 16);
    dot->start_cluster_low = (new_cluster & 0xFFFF);

    // .. entry 
    for (int i = 0; i < 8; i++) dotdot->filename[i] = ' ';
    for (int i = 0; i < 3; i++) dotdot->extension[i] = ' ';
    dotdot->filename[0] = '.'; dotdot->filename[1] = '.';
    dotdot->attributes = ATTR_DIRECTORY;
    dotdot->start_cluster_high = (parent_cluster >> 16);
    dotdot->start_cluster_low = (parent_cluster & 0xFFFF);

    if (realfs_write_cluster(vol, new_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return false;
    }
    kfree(cluster_buf);

    // Find free entry in parent directory
    uint32_t search_cluster = parent_cluster;
    uint8_t *sect_buf = (uint8_t*)kmalloc(512);
    bool found_free = false;
    uint32_t free_sector = 0;
    uint32_t free_offset = 0;

    uint8_t *dir_cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!dir_cluster_buf) {
        kfree(sect_buf);
        return false;
    }

    while (search_cluster < 0x0FFFFFF8 && !found_free) {
        if (realfs_read_cluster(vol, search_cluster, dir_cluster_buf) != 0) break;
        FAT32_DirEntry *entries = (FAT32_DirEntry*)dir_cluster_buf;
        int count = cluster_size / 32;
        for (int e = 0; e < count; e++) {
            if (entries[e].filename[0] == 0 || entries[e].filename[0] == 0xE5) {
                uint32_t lba = vol->cluster_begin_lba + (search_cluster - 2) * vol->sectors_per_cluster;
                free_sector = lba + (e * 32) / 512;
                free_offset = (e * 32) % 512;
                found_free = true;
                break;
            }
        }
        if (!found_free) {
            uint32_t next = realfs_next_cluster(vol, search_cluster);
            if (next >= 0x0FFFFFF8) {
                // Need to expand directory? 
                uint32_t expanded = realfs_allocate_cluster(vol);
                if (expanded == 0) break;
                // Link, preserve high bits
                uint32_t fat_sector = vol->fat_begin_lba + (search_cluster * 4) / 512;
                uint32_t fat_offset = (search_cluster * 4) % 512;
                if (vol->disk->read_sector(vol->disk, fat_sector, sect_buf) == 0) {
                    uint32_t old_val = *(uint32_t*)&sect_buf[fat_offset];
                    *(uint32_t*)&sect_buf[fat_offset] = (old_val & 0xF0000000) | (expanded & 0x0FFFFFFF);
                    vol->disk->write_sector(vol->disk, fat_sector, sect_buf);
                    if (vol->cached_fat_sector == fat_sector) vol->cached_fat_sector = 0xFFFFFFFF;
                }
                // Zero out new cluster
                for(uint32_t k=0; k<cluster_size; k++) dir_cluster_buf[k] = 0;
                realfs_write_cluster(vol, expanded, dir_cluster_buf);
                next = expanded;
            }
            search_cluster = next;
        }
    }
    kfree(dir_cluster_buf);

    if (found_free) {
        vol->disk->read_sector(vol->disk, free_sector, sect_buf);
        FAT32_DirEntry *d = (FAT32_DirEntry*)(sect_buf + free_offset);
        char dos_name[11];
        to_dos_filename(dirname, dos_name);
        for (int k = 0; k < 8; k++) d->filename[k] = dos_name[k];
        for (int k = 0; k < 3; k++) d->extension[k] = dos_name[8+k];
        d->attributes = ATTR_DIRECTORY;
        d->start_cluster_high = (new_cluster >> 16);
        d->start_cluster_low = (new_cluster & 0xFFFF);
        d->file_size = 0;
        vol->disk->write_sector(vol->disk, free_sector, sect_buf);
    }

    kfree(sect_buf);
    return found_free;
}

bool fat32_mkdir(const char *path) {
    const char *p = path;
    char drive = parse_drive_from_path(&p);
    
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));

    if (drive != 'A') {
        bool res = realfs_mkdir(drive, p);
        wm_notify_fs_change();
        asm volatile("push %0; popfq" : : "r"(rflags));
        return res;
    }

    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(p, normalized);
    
    if (ramfs_find_file(normalized)) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return false; 
    }
    
    if (!check_desktop_limit(normalized)) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return false;
    }
    
    FileEntry *entry = ramfs_find_free_entry();
    if (!entry) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return false;
    }
    
    entry->used = true;
    fs_strcpy(entry->full_path, normalized);
    extract_filename(normalized, entry->filename);
    extract_parent_path(normalized, entry->parent_path);
    entry->start_cluster = ramfs_allocate_cluster();
    entry->size = 0;
    entry->attributes = ATTR_DIRECTORY;
    
    wm_notify_fs_change();
    asm volatile("push %0; popfq" : : "r"(rflags));
    return true;
}

bool fat32_rmdir(const char *path) {
    if (parse_drive_from_path(&path) != 'A') return false;
    
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    FileEntry *entry = ramfs_find_file(normalized);
    if (!entry || !(entry->attributes & ATTR_DIRECTORY)) {
        asm volatile("push %0; popfq" : : "r"(rflags));
        return false;
    }
    
    entry->used = false;
    wm_notify_fs_change();
    asm volatile("push %0; popfq" : : "r"(rflags));
    return true;
}

bool fat32_delete(const char *path) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    const char *p = path;
    char drive = parse_drive_from_path(&p);
    
    bool result = false;
    
    if (drive == 'A') {
        // RAMFS deletion
        char normalized[FAT32_MAX_PATH];
        fat32_normalize_path(p, normalized);
        
        FileEntry *entry = ramfs_find_file(normalized);
        if (!entry || (entry->attributes & ATTR_DIRECTORY)) {
            asm volatile("push %0; popfq" : : "r"(rflags));
            return false;
        }
        
        entry->used = false;
        wm_notify_fs_change();
        result = true;
    } else {
        // Real FAT32 deletion
        result = realfs_delete(drive, p);
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return result;
}

int fat32_get_info(const char *path, FAT32_FileInfo *info) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    const char *p = path;
    char drive = parse_drive_from_path(&p);
    
    int result = -1;
    if (drive == 'A') {
        char normalized[FAT32_MAX_PATH];
        fat32_normalize_path(p, normalized);
        FileEntry *entry = ramfs_find_file(normalized);
        if (entry) {
            fs_strcpy(info->name, entry->filename);
            info->size = entry->size;
            info->is_directory = (entry->attributes & ATTR_DIRECTORY) != 0;
            info->start_cluster = entry->start_cluster;
            result = 0;
        }
    } else {
        FAT32_FileHandle *fh = realfs_open(drive, p, "r");
        if (fh) {

            extract_filename(p, info->name);
            info->size = fh->size;
            info->start_cluster = fh->start_cluster;
            
            if (fs_strcmp(p, "/") == 0 || fs_strcmp(p, "") == 0) {
                info->is_directory = 1;
            } else {

                info->is_directory = fat32_is_directory(path);
            }
            fat32_close(fh);
            result = 0;
        }
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return result;
}

bool fat32_exists(const char *path) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    const char *p = path;
    char drive = parse_drive_from_path(&p);
    
    bool exists = false;
    if (drive == 'A') {
        char normalized[FAT32_MAX_PATH];
        fat32_normalize_path(p, normalized);
        exists = (ramfs_find_file(normalized) != NULL);
    } else {
        // RealFS check
        FAT32_FileHandle *fh = realfs_open(drive, p, "r");
        if (fh) {
            exists = true;
            fat32_close(fh);
        }
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return exists;
}

bool fat32_rename(const char *old_path, const char *new_path) {
    // Only A: supported for rename/modify
    if (parse_drive_from_path(&old_path) != 'A') return false;
    if (parse_drive_from_path(&new_path) != 'A') return false;

    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    FileEntry *entry = ramfs_find_file(old_path); // Need to normalize inside find? yes ramfs_find calls normalize
    if (!entry) { asm volatile("push %0; popfq" : : "r"(rflags)); return false; }
    
    // Check destination
    if (ramfs_find_file(new_path)) { asm volatile("push %0; popfq" : : "r"(rflags)); return false; }

    size_t old_len = fs_strlen(old_path);
    // Logic from original rename...
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) continue;
        if (fs_strcmp(files[i].full_path, old_path) == 0) {
            fs_strcpy(files[i].full_path, new_path);
            extract_filename(new_path, files[i].filename);
            extract_parent_path(new_path, files[i].parent_path);
        } else if (fs_strlen(files[i].full_path) > old_len && 
                   fs_starts_with(files[i].full_path, old_path) &&
                   files[i].full_path[old_len] == '/') {
            char suffix[FAT32_MAX_PATH];
            fs_strcpy(suffix, files[i].full_path + old_len);
            fs_strcpy(files[i].full_path, new_path);
            fs_strcat(files[i].full_path, suffix);
        }
        if (fs_strcmp(files[i].parent_path, old_path) == 0) {
            fs_strcpy(files[i].parent_path, new_path);
        } else if (fs_strlen(files[i].parent_path) > old_len &&
                   fs_starts_with(files[i].parent_path, old_path) &&
                   files[i].parent_path[old_len] == '/') {
            char suffix[FAT32_MAX_PATH];
            fs_strcpy(suffix, files[i].parent_path + old_len);
            fs_strcpy(files[i].parent_path, new_path);
            fs_strcat(files[i].parent_path, suffix);
        }
    }
    wm_notify_fs_change();
    asm volatile("push %0; popfq" : : "r"(rflags));
    return true;
}

bool fat32_is_directory(const char *path) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    const char *p = path;
    char drive = parse_drive_from_path(&p);
    
    bool is_dir = false;
    if (drive == 'A') {
        char normalized[FAT32_MAX_PATH];
        fat32_normalize_path(p, normalized);
        FileEntry *entry = ramfs_find_file(normalized);
        is_dir = (entry && (entry->attributes & ATTR_DIRECTORY));
    } else {
        FAT32_FileHandle *fh = realfs_open(drive, p, "r");
        if (fh) {
       
            if (fs_strcmp(p, "/") == 0 || fs_strcmp(p, "") == 0) is_dir = true;
            else {

            }
            fat32_close(fh);
        }
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return is_dir;
}

int fat32_list_directory(const char *path, FAT32_FileInfo *entries, int max_entries) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    const char *p = path;
    char drive = parse_drive_from_path(&p);
    
    int count = 0;
    if (drive == 'A') {
        char normalized[FAT32_MAX_PATH];
        fat32_normalize_path(p, normalized);
        

        
        for (int i = 0; i < MAX_FILES && count < max_entries; i++) {
            if (files[i].used && fs_strcmp(files[i].parent_path, normalized) == 0) {
                fs_strcpy(entries[count].name, files[i].filename);
                entries[count].size = files[i].size;
                entries[count].is_directory = (files[i].attributes & ATTR_DIRECTORY) != 0;
                entries[count].start_cluster = files[i].start_cluster;
                count++;
            }
        }
    } else {
        count = realfs_list_directory(drive, p, entries, max_entries);
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return count;
}

bool fat32_chdir(const char *path) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    const char *p = path;
    char drive = parse_drive_from_path(&p);
    
    if (path[0] && path[1] == ':') {
         if (disk_get_by_letter(drive)) {
             current_drive = drive;
             current_dir[0] = '/';
             current_dir[1] = 0;
             // If just switching drive (e.g. "B:"), return true
             if (p[0] == 0) {
                 asm volatile("push %0; popfq" : : "r"(rflags));
                 return true;
             }
         } else {
             asm volatile("push %0; popfq" : : "r"(rflags));
             return false;
         }
    }
    
    if (fat32_is_directory(path)) {
         if (drive == 'A') {
             char normalized[FAT32_MAX_PATH];
             fat32_normalize_path(p, normalized);
             fs_strcpy(current_dir, normalized);
         } else {
             fs_strcpy(current_dir, p); 
             if (current_dir[0] != '/') {

             }
         }
         asm volatile("push %0; popfq" : : "r"(rflags));
         return true;
    }
    
    asm volatile("push %0; popfq" : : "r"(rflags));
    return false;
}

void fat32_get_current_dir(char *buffer, int size) {
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    int len = 0;
    buffer[0] = current_drive;
    buffer[1] = ':';
    len = 2;
    
    int dir_len = fs_strlen(current_dir);
    if (len + dir_len >= size) dir_len = size - len - 1;
    
    for (int i = 0; i < dir_len; i++) {
        buffer[len + i] = current_dir[i];
    }
    buffer[len + dir_len] = 0;
    asm volatile("push %0; popfq" : : "r"(rflags));
}