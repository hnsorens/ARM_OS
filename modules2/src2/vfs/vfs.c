#include "module.h"

#include "str/str_inc.h"
#include "kmm/kmm_inc.h"

#include <stdint.h>

typedef enum entry_type_t {
  FT_UNKNOWN,
  FT_REG_FILE,
  FT_DIR,
} entry_type_t;

typedef struct list_head_t
{
    struct list_head_t* next; ///< Pointer to next list node
    struct list_head_t* prev; ///< Pointer to previous list node
} list_head_t;

typedef void ext2_inode;

typedef uint64_t (*file_ops_t)(uint64_t asd, uint64_t arg1, uint64_t arg2);

typedef struct vfs_entry_t
{
    uint32_t inode_num;         ///< Filesystem-specific inode number
    void* file_data;          ///< Pointer to underlying inode structure
    char* name;                 ///< Entry name
    struct vfs_entry_t* parent; ///< Pointer to parent directory
    list_head_t children;       ///< List of child entries
    list_head_t sibling;        ///< Sibling entries in parent directory
    entry_type_t type;          ///< Entry type (file, dir, etc.)
    uint32_t name_hash;         ///< Precomputed name hash
    uint8_t children_loaded;    ///< Flag indicating children were loaded
    file_ops_t* ops;            ///< File operations table
    void* private_data;         ///< Private data for special files
} vfs_entry_t;

#define offsetof(t, d) __builtin_offsetof(t, d)
#define container_of(ptr, type, member) ((type*)((char*)(ptr)-offsetof(type, member)))

void vfs_init() {}

void init() {}

void vfs_fetch() {}

static uint32_t fnv1a_hash(const char* str)
{
    uint32_t hash = 2166136261u;
    while (*str)
    {
        hash ^= (uint8_t)(*str++);
        hash *= 16777619u;
    }
    return hash;
}

static void init_list_head(list_head_t* list)
{
    list->next = list;
    list->prev = list;
}

static vfs_entry_t* vfs_find_child(vfs_entry_t* parent, const char* name)
{
    list_head_t* pos;
    for (pos = parent->children.next; pos != &parent->children; pos = pos->next)
    {
        vfs_entry_t* child = container_of(pos, vfs_entry_t, sibling);
        if (str_strcmp(child->name, name) == 0)
        {
            return child;
        }
    }
    return 0;
}

static void list_add_tail(list_head_t* new_node, list_head_t* head)
{
    new_node->prev = head->prev;
    new_node->next = head;
    head->prev->next = new_node;
    head->prev = new_node;
}

static int vfs_entry_has_children(vfs_entry_t* entry)
{
    return entry->children.next != &entry->children;
}

void vfs_entry_init(vfs_entry_t* entry, const char* name)
{
    entry->name = kmm_kmalloc(str_strlen(name) + 1);
    str_memcpy(entry->name, name, str_strlen(name) + 1);
    entry->inode_num = 0;
    entry->parent = 0;
    entry->children_loaded = 0;
    entry->type = FT_DIR;
    entry->name_hash = fnv1a_hash(name);
    init_list_head(&entry->children);
    init_list_head(&entry->sibling);
}

void vfs_add_child(vfs_entry_t* parent, vfs_entry_t* child)
{
    child->parent = parent;
    list_add_tail(&child->sibling, &parent->children);
}

void vfs_init2()
{
    /* /\* Parse GPT partitions *\/ */
    /* GPTPartition* partitions = KERNEL_ParseGPTHeader(1); */
    /* GPTPartition* filesystemPartition = &partitions[1]; */

    /* /\* Initialize EXT2 filesystem *\/ */
    /* if (ext2_init(FILESYSTEM, KERNEL_ReadSectors, KERNEL_WriteSectors, filesystemPartition->first_lba, filesystemPartition->last_lba) != 0) */
    /* { */
    /*     /\* Handle initialization failure *\/ */
    /* } */

    /* /\* Initialize root directory *\/ */
    /* vfs_entry_init(ROOT, ""); */
    /* ROOT->inode_num = 2; */

    /* /\* Create /dev directory *\/ */
    /* *DEV = vfs_create_entry(ROOT, "dev", EXT2_FT_DIR); */
    /* (*DEV)->children_loaded = 1; */
}

unsigned long vfs_write_reg_file(uint64_t open_file, uint64_t buf, size_t size)
{
    return ext2_file_write(FILESYSTEM, (file_descriptor_t*)open_file, (uint8_t*)buf, size);
}

unsigned long vfs_read_reg_file(uint64_t open_file, uint64_t buf, size_t size)
{
    return ext2_file_read(FILESYSTEM, (file_descriptor_t*)open_file, (uint8_t*)buf, size);
}

void vfs_populate_directory(vfs_entry_t* dir)
{
    if (dir->type != EXT2_FT_DIR || dir->children_loaded)
        return;
    /* Iterate through directory entries */
    ext2_dirent_t* dirent;
    ext2_dirent_iter_t iter;
    ext2_dir_iter_start(FILESYSTEM, &iter, dir->inode_num);

    while (ext2_dir_iter_next(FILESYSTEM, &iter, &dirent) == 0)
    {
        /* Create new VFS entry */
        vfs_entry_t* entry = pool_allocate(*VFS_ENTRY_POOL);
        vfs_entry_init(entry, dirent->name);
        entry->type = dirent->file_type;
        entry->inode_num = dirent->inode;
        entry->parent = dir;
        entry->inode = pool_allocate(*INODE_POOL);
        // TODO: Make a better way for dynamicall setting callbacks, fornow there are 8
        entry->ops = kmalloc(sizeof(void*) * 8);
        vfs_add_child(dir, entry);

        if (entry->type == EXT2_FT_REG_FILE)
        {
            entry->ops[DEV_WRITE] = vfs_write_reg_file;
            entry->ops[DEV_READ] = vfs_read_reg_file;
        }
    }

    ext2_dir_iter_end(&iter);
}

int vfs_find_entry(vfs_entry_t* current, vfs_entry_t** out, const char* path)
{
    if (!current || !path || !*path)
        return 1;

    /* Copy path to modifiable buffer */
    kernel_strcpy(PATH, path);
    char* path_ptr = PATH;

    /* Handle absolute paths */
    if (path_ptr[0] == '/')
    {
        current = ROOT;
        path_ptr++;
    }

    /* Process each path component */
    while (*path_ptr)
    {
        /* Extract next component */
        char* next_slash = path_ptr;
        while (*next_slash && *next_slash != '/')
            next_slash++;

        char component[256];
        size_t len = next_slash - path_ptr;
        if (len >= sizeof(component))
            return 1;

        kmemcpy(component, path_ptr, len);
        component[len] = '\0';

        /* Handle . and .. special directories */
        if (kernel_strcmp(component, ".") == 0)
        {
            /* Current directory - no action needed */
        }
        else if (kernel_strcmp(component, "..") == 0)
        {
            /* Parent directory */
            if (current->parent)
                current = current->parent;
        }
        else
        {
            /* Lazy-load children if needed */
            if (!current->children_loaded)
            {
                vfs_populate_directory(current);
                current->children_loaded = 1;
            }

            /* Find child entry */
            vfs_entry_t* next = vfs_find_child(current, component);
            if (!next)
            {
                return 1;
            }

            current = next;
        }

        path_ptr = *next_slash ? next_slash + 1 : next_slash;
    }

    *out = current;
    return 0;
}

file_descriptor_t* vfs_open_file(vfs_entry_t* entry)
{
    return fdm_open_file(entry);
}

vfs_entry_t* vfs_create_entry(vfs_entry_t* dir, const char* name, entry_type_t type)
{

    vfs_entry_t* entry = pool_allocate(*VFS_ENTRY_POOL);
    vfs_entry_init(entry, name);
    entry->type = type;
    entry->inode_num = -1;
    entry->parent = dir;
    entry->inode = pool_allocate(*INODE_POOL);
    // TODO: Make a better way for dynamicall setting callbacks, fornow there are 8
    entry->ops = kmalloc(sizeof(void*) * 8);
    vfs_add_child(dir, entry);
    return entry;
}

void vfs_path(vfs_entry_t* dir, char* buffer, uint64_t* offset)
{
    if (dir->parent)
    {
        vfs_path(dir->parent, buffer, offset);
    }

    kernel_strcat(&buffer[*offset], dir->name);
    *offset += kernel_strlen(dir->name);
    buffer[*offset] = '\0';
    kernel_strcat(&buffer[*offset], "/");
    *offset += 1;
    buffer[*offset] = '\0';
}
