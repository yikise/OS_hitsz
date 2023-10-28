// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCKETS 13
struct {
  struct spinlock global_lock;
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;

void
binit(void)
{
  struct buf *b;
  initlock(&bcache.global_lock, "bcache_global");
  //初始化hashbucket
  for(int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
}

  // Create linked list of buffers
  //初始化buffer
  for (b = bcache.buf; b < bcache.buf+NBUF; b++) {
    int hashNumber = b->blockno % NBUCKETS;
    b->next = bcache.hashbucket[hashNumber].next;
    b->prev = &bcache.hashbucket[hashNumber];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[hashNumber].next->prev = b;
    bcache.hashbucket[hashNumber].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  //Hash(blockno)的过程
  int hashNumber = blockno % NBUCKETS;
  acquire(&bcache.lock[hashNumber]);

  // Is the block already cached?
  for(b = bcache.hashbucket[hashNumber].next; b != &bcache.hashbucket[hashNumber]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hashNumber]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  
  //首先释放原先哈希桶的锁
  release(&bcache.lock[hashNumber]);

  //获取全局锁
  acquire(&bcache.global_lock);

  //获取哈希桶的锁
  acquire(&bcache.lock[hashNumber]);

  // Recycle the least recently used (LRU) unused buffer.
  //循环遍历NBUCKETS个哈希桶
  for (int i = 0; i < NBUCKETS; i++) {
    //获取当前哈希桶的锁
    if (i != hashNumber) {
      acquire(&bcache.lock[i]);
    }
    for(b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev){
      if(b->refcnt == 0) {
        //若当前哈希桶不是原先哈希桶，
        //则需要把搜索到的空闲buffer移到我们原先的哈希桶中
        if(i != hashNumber) {
          b->next->prev = b->prev;
          b->prev->next = b->next;
          //释放该哈希桶的锁
          release(&bcache.lock[i]);
          b->next = bcache.hashbucket[hashNumber].next;
          b->prev = &bcache.hashbucket[hashNumber];
          bcache.hashbucket[hashNumber].next->prev = b;
          bcache.hashbucket[hashNumber].next = b;
        }
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        
        release(&bcache.lock[hashNumber]);
        release(&bcache.global_lock); //释放全局锁

        acquiresleep(&b->lock);
        return b;
      }
    }
    if (i != hashNumber) {
      release(&bcache.lock[i]);
    }
  }
  release(&bcache.lock[hashNumber]);
  release(&bcache.global_lock); //释放全局锁

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  //获取hashNumber
  int hashNumber = b->blockno % NBUCKETS;
  //获取当前哈希桶的锁
  acquire(&bcache.lock[hashNumber]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;

    b->next = bcache.hashbucket[hashNumber].next;
    b->prev = &bcache.hashbucket[hashNumber];
    bcache.hashbucket[hashNumber].next->prev = b;
    bcache.hashbucket[hashNumber].next = b;
  }
  release(&bcache.lock[hashNumber]);
}

void
bpin(struct buf *b) {
  //获取hashNumber
  int hashNumber = b->blockno % NBUCKETS;
  acquire(&bcache.lock[hashNumber]);
  b->refcnt++;
  release(&bcache.lock[hashNumber]);
}

void
bunpin(struct buf *b) {
  //获取hashNumber
  int hashNumber = b->blockno % NBUCKETS;
  acquire(&bcache.lock[hashNumber]);
  b->refcnt--;
  release(&bcache.lock[hashNumber]);
}