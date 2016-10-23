#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "vqueue.h"

vq_t * vq_init(const char * name, size_t min_capacity) {
    vq_t * vq = calloc(1, sizeof(*vq));
    if(vq == NULL)
        goto error;

    // Round up capacity to multiple of pagesize
    size_t pagesize = sysconf(_SC_PAGE_SIZE);

    min_capacity--;
    for(size_t i = 1ul; i < sizeof(min_capacity) * CHAR_BIT; i <<= 1)
        min_capacity |= min_capacity >> i;
    min_capacity++;

    vq->capacity = ((min_capacity + pagesize - 1ul) & ~(pagesize - 1ul));
    vq->small_mask = vq->capacity-1ul;
    vq->big_mask   = vq->small_mask << 1;

    //TODO: remove me
    shm_unlink(name);

    // Set up shared memory fd
    vq->_fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0644);
    if(vq->_fd < 0)
        goto error;

    if(ftruncate(vq->_fd, vq->capacity) != 0)
        goto error;

    // Create double mapping
    vq->_buffer_start = mmap(NULL, vq->big_mask + 1,
            PROT_NONE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if(!vq->_buffer_start || vq->_buffer_start == MAP_FAILED)
        goto error;

    {
        void *_buffer_start = mmap(vq->_buffer_start, vq->capacity, 
                PROT_READ | PROT_WRITE, MAP_SHARED|MAP_FIXED, vq->_fd, 0);

        if(_buffer_start == NULL || _buffer_start == MAP_FAILED)
            goto error;
    }
    {
        vq->_buffer_middle = mmap(vq->_buffer_start + vq->capacity, vq->capacity, 
                PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, vq->_fd, 0);

        if(vq->_buffer_middle == NULL || vq->_buffer_middle == MAP_FAILED)
            goto error;
    }
    vq->buffer = vq->_buffer_start;
    return vq;

error:
    if(vq) {
        //if(vq->_fd > 0) TODO

        if(vq->_buffer_start)
            munmap(vq->_buffer_start, vq->capacity * 2);

        free(vq);
        vq = NULL;
    }
    return vq;
}

void vq_destroy(vq_t * vq) {
    //vq->fd TODO
    munmap(vq->_buffer_start, vq->capacity * 2);
    free(vq);
}
size_t vq_zcr_start(vq_t * vq, void const ** read_ptr) {
    // Calculate where to start writing
    if(!vq)
        return 0;

    ptrdiff_t rptr = atomic_load(&vq->rptr);
    ptrdiff_t wptr = atomic_load(&vq->wptr);

    size_t space = (wptr - rptr) & vq->big_mask;
    if(read_ptr){
        if(!space)
            *read_ptr = NULL;
        else{
            *read_ptr = ((unsigned char*)vq->_buffer_start) + (rptr & vq->small_mask);
        }
    }
    // Return the amount of free space
    return space;
}

size_t vq_zcr_end(vq_t * vq, size_t length) {
    if(!vq || !length)
        return 0;

    ptrdiff_t rptr = atomic_load(&vq->rptr);
    ptrdiff_t wptr = atomic_load(&vq->wptr);

    size_t space = (wptr - rptr) & vq->big_mask;
    if(space > length)
        length = space;
    atomic_fetch_add(&vq->rptr, length);
    return length;
}

size_t vq_zcw_start(vq_t * vq, void ** write_ptr) {
    // Calculate where to start writing
    if(!vq)
        return 0;
    ptrdiff_t rptr = atomic_load(&vq->rptr);
    ptrdiff_t wptr = atomic_load(&vq->wptr);

    size_t space = (rptr + vq->capacity - wptr) & vq->big_mask;
    if(write_ptr) {
        if(!space)
            *write_ptr = NULL;
        else{
            *write_ptr = ((unsigned char*)vq->_buffer_start) + (wptr & vq->small_mask);
        }
    }
    // Return the amount of free space
    return space;
}

size_t vq_zcw_end(vq_t * vq, size_t length) {
    if(!vq || !length)
        return 0;

    ptrdiff_t rptr = atomic_load(&vq->rptr);
    ptrdiff_t wptr = atomic_load(&vq->wptr);

    size_t space = (rptr + vq->capacity - wptr) & vq->big_mask;
    if(space > length)
        length = space;
    atomic_fetch_add(&vq->wptr, length);
    return length;
}
size_t vq_generic_read(vq_t *vq, vq_read_fn *read_fn, void *opaque, size_t length)
{
    if(!vq)
        return 0;
    const void *buffer = NULL;
    size_t space = vq_zcr_start(vq, &buffer);
    if(space < length)
        length = space;

    if(read_fn) {
        return vq_zcr_end(vq, (*read_fn)(opaque, buffer, length));
    }else{
        return vq_zcr_end(vq, length);
    }
}

size_t vq_generic_write(vq_t *vq, vq_write_fn *write_fn, void *opaque, size_t length)
{
    if(!vq || !write_fn)
        return 0;
    void *buffer = NULL;
    size_t space = vq_zcw_start(vq, &buffer);
    if(space < length)
        length = space;
    return vq_zcw_end(vq, (*write_fn)(opaque,buffer,length));
}
static size_t write_cb(void *opaque, void *data, size_t length)
{
    if(data && opaque && length)
        memmove(data,opaque,length);
    return length;
}
static size_t read_cb(void *opaque, void const *data, size_t length)
{
    if(data && opaque && length)
        memmove(opaque,data,length);
    return length;
}
size_t vq_write(vq_t * vq, const void * data, size_t length) {
    return vq_generic_write(vq, &write_cb,(void*)data, length);
}

size_t vq_read(vq_t * vq, void * read_ptr, size_t length) {
    return vq_generic_read(vq, &read_cb,read_ptr, length);
}
