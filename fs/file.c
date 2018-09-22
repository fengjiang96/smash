#include "sys/types.h"
#include "path.h"
#include "fsdefs.h"
#include "inodes.h"
#include "zones.h"
#include "sys/stat.h"
#include "string.h"
#include "buffer.h"
#include "log.h"
#include "file.h"

static int
_file_append(IndexNode *inode, void *data, int len)
{
    if (len > BLOCK_SIZE )
        return -1;
    off_t tail = inode->in_inode.in_file_size;
    blk_t blk = get_zone(inode, tail);
    int offset = tail & (BLOCK_SIZE - 1);
    int less = BLOCK_SIZE - offset;
    int more = len - less;

    BlockBuffer *buf = get_block(inode->in_dev, blk);
    memcpy(buf->bf_data + offset, data, MIN(len, less));
    buf->bf_status |= BUF_DIRTY;
    release_block(buf);

    if (more > 0) {
        blk_t new_blk = alloc_zone(inode);
        BlockBuffer *new_buf = get_block(inode->in_dev, new_blk);
        memcpy(new_buf->bf_data, data+less, more);
        new_buf->bf_status |= BUF_DIRTY;
        release_block(new_buf);
    }
    inode->in_inode.in_file_size += len;
    inode->in_status |= INODE_DIRTY;

    return 0;
}

static void
_add_entry(IndexNode *dinode, const char *fname, ino_t ino)
{
    Direction dir;
    strcpy(dir.dr_name, fname);
    dir.dr_inode = ino;
    _file_append(dinode, &dir, sizeof(Direction));
    dinode->in_inode.in_num_links += 1;
}

IndexNode *
file_open(const char *pathname, int flags, int mode)
{
    IndexNode *inode = name_to_inode(pathname);
    if (inode == NULL)
        return NULL;

    return inode;
};

IndexNode *
file_create(const char *pathname, int flags, int mode)
{
    const char *basename = NULL;
    IndexNode *ret_inode = NULL;
    IndexNode *dinode = name_to_dirinode(pathname, &basename);
    if (dinode == NULL) return NULL;

    ino_t ino = search_file(dinode, basename, FILENAME_LEN);
    if (ino == INVALID_INODE) {
        // create new file
        ret_inode = alloc_inode(dinode->in_dev);
        ret_inode->in_inode.in_file_mode = mode;
        ret_inode->in_inode.in_num_links = 1;
        ret_inode->in_inode.in_file_size = 0;

        _add_entry(dinode, basename, ret_inode->in_inum);
    }
    else {
        ret_inode = get_inode(dinode->in_dev, ino);
    }
    release_inode(dinode);

    return ret_inode;
}

ssize_t
file_read(const IndexNode *inode, off_t seek, void *buf, size_t count)
{
    ssize_t ret = 0;
    if (inode->in_inode.in_file_size < seek)
        return ret;
    count = MIN(inode->in_inode.in_file_size - seek + 1, count);
    if (count > 0) {
        blk_t blk = get_zone(inode, seek);
        BlockBuffer *blkbuf = get_block(inode->in_dev, blk);
        uint32_t offset = seek & (BLOCK_SIZE - 1);
        uint32_t len = MIN(BLOCK_SIZE - offset, count);
        memcpy(buf, blkbuf->bf_data + offset, len);
        release_block(blkbuf);
        ret += len;
    }
    if (ret < count) {
        blk_t blk = get_zone(inode, seek + ret);
        BlockBuffer *blkbuf = get_block(inode->in_dev, blk);
        uint32_t len = count - ret;
        memcpy(buf + ret, blkbuf->bf_data, len);
        release_block(blkbuf);
        ret += len;
    }

    return ret;
}

ssize_t
file_write(IndexNode *inode, off_t seek, const void *buf, size_t count)
{
    ssize_t ret = 0;
    const blk_t new_blknum = (seek + count + BLOCK_SIZE - 1) >> BLOCK_LOG_SIZE;
    const blk_t cur_blknum = (inode->in_inode.in_file_size + BLOCK_SIZE - 1)>> BLOCK_LOG_SIZE;
    if (new_blknum > cur_blknum) {
        alloc_zone(inode);
    }
    blk_t blk = get_zone(inode, seek);
    BlockBuffer *blkbuf = get_block(inode->in_dev, blk);
    uint32_t offset = seek & (BLOCK_SIZE - 1);
    uint32_t remain = MIN(BLOCK_SIZE - offset, count);
    memcpy(blkbuf->bf_data + offset, buf, remain);
    blkbuf->bf_status |= BUF_DIRTY;
    release_block(blkbuf);
    ret += remain;

    uint32_t more = count - remain;
    if (more > 0) {
        blk_t next_blk = get_zone(inode, seek + ret);
        BlockBuffer *new_buf = get_block(inode->in_dev, next_blk);
        memcpy(new_buf->bf_data, buf + ret, more);
        new_buf->bf_status |= BUF_DIRTY;
        release_block(new_buf);
        ret += more;
    }
    inode->in_inode.in_file_size = MAX(inode->in_inode.in_file_size, seek + ret);
    inode->in_status |= INODE_DIRTY;
    return ret;
}

int
file_close(IndexNode *inode)
{
    release_inode(inode);
    return 0;
}

int
file_trunc(IndexNode *inode)
{
    return truncate_zones(inode);
}

int
file_link(const char *pathname, IndexNode *inode)
{
    int ret = 0;
    const char *basename = NULL;
    IndexNode *dinode = name_to_dirinode(pathname, &basename);
    if (dinode == NULL) return NULL;

    ino_t ino = search_file(dinode, basename, FILENAME_LEN);
    if (ino == INVALID_INODE) {
        _add_entry(dinode, basename, inode->in_inum);
    }
    else {
        ret = -1;
    }
    release_inode(dinode);
    return ret;
}

int
file_unlink(const char *pathname, IndexNode *inode)
{
    int ret = 0;
    return ret;
}
